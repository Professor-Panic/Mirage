#pragma once
// ---------------------------------------------------------------------------
// FileDialog - a small Open / Save As dialog drawn with RUI.
//
//   FileDialog fileDialog(ui);                      // once, after creating ui
//
//   fileDialog.Open(".scene", "scenes", [&](const std::string& path) { ... });
//   fileDialog.Save(".scene", "scenes", "test.scene", [&](const std::string& path) { ... });
//
//   // every frame, before ui.Update():
//   fileDialog.Update();
//
// Open: lists directories + files whose extension matches, returns the picked file.
// Save: same listing, plus a name field; the extension is appended if missing and
//       an existing file needs a second press of Save to be overwritten.
// An empty extension shows every file. Hidden entries (".something") are skipped.
//
// The markup side is the `file-dialog` container in main.rxml. It talks to this
// class only through the bindings registered in the constructor:
//   fd_open fd_save fd_title fd_dir fd_name fd_status   (read by the markup)
//   fd_clicked fd_action                                (written by the markup)
//   FileEntries                                         (list rows: index, label)
// ---------------------------------------------------------------------------

#include "rui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

class FileDialog
{
public:
    using Callback = std::function<void(const std::string& path)>;

    explicit FileDialog(rui::Context& ui) : ui_(ui)
    {
        auto& rt = ui_.runtime;
        rt.Bind("fd_open", open_);
        rt.Bind("fd_save", save_);
        rt.Bind("fd_title", title_);
        rt.Bind("fd_dir", dirText_);
        rt.Bind("fd_name", name_);
        rt.Bind("fd_status", status_);
        rt.Bind("fd_clicked", clicked_);
        rt.Bind("fd_action", action_);

        rt.BindList(
            "FileEntries",
            [this]() { return rows_; },
            [](auto& list)
            {
                list.Field("index", &Entry::index)
                    .Field("label", &Entry::label);
            });
    }

    FileDialog(const FileDialog&) = delete;
    FileDialog& operator=(const FileDialog&) = delete;

    void Open(std::string extension, const std::string& startDir, Callback onPick,
              std::string title = "Open")
    {
        Show(false, std::move(extension), startDir, "", std::move(onPick), std::move(title));
    }

    void Save(std::string extension, const std::string& startDir, std::string defaultName,
              Callback onPick, std::string title = "Save As")
    {
        Show(true, std::move(extension), startDir, std::move(defaultName), std::move(onPick),
             std::move(title));
    }

    bool IsOpen() const { return open_ == 1; }

    // Call once per frame. Applies whatever the markup wrote since last frame.
    void Update()
    {
        if (open_ != 1)
        {
            clicked_ = -1;
            action_ = 0;
            return;
        }
        if (clicked_ >= 0)
        {
            const int index = clicked_;
            clicked_ = -1;
            Click(index);
        }
        if (action_ != 0)
        {
            const int action = action_;
            action_ = 0;
            Act(action);
        }
    }

private:
    // One visible row. `index` points back into items_.
    struct Entry
    {
        int index = 0;
        std::string label;
    };
    struct Item
    {
        std::filesystem::path path;
        bool isDir = false;
    };

    // fd_action values written by the markup buttons.
    enum Action { kNone = 0, kConfirm = 1, kCancel = 2, kUp = 3 };

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::string Trim(const std::string& s)
    {
        const auto first = s.find_first_not_of(" \t");
        if (first == std::string::npos) return {};
        return s.substr(first, s.find_last_not_of(" \t") - first + 1);
    }

    static std::string NormalizeExt(std::string ext)
    {
        ext = Lower(Trim(ext));
        if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
        return ext;
    }

    // Paths inside the working directory come back relative ("scenes/a.scene"),
    // everything else absolute. Always forward slashes.
    static std::string ProjectPath(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path rel = std::filesystem::relative(path, std::filesystem::current_path(ec), ec);
        if (!ec && !rel.empty() && rel.begin()->string() != "..")
            return rel.generic_string();
        return path.generic_string();
    }

    bool Matches(const std::filesystem::path& path) const
    {
        return ext_.empty() || Lower(path.extension().string()) == ext_;
    }

    void Show(bool save, std::string ext, const std::string& startDir, std::string name,
              Callback cb, std::string title)
    {
        save_ = save ? 1 : 0;
        ext_ = NormalizeExt(std::move(ext));
        cb_ = std::move(cb);
        title_ = std::move(title);
        name_ = std::move(name);
        status_.clear();
        overwriteArmed_.clear();
        selected_.clear();
        clicked_ = -1;
        action_ = 0;
        SetDir(startDir);
        open_ = 1;
        Refresh();
    }

    void SetDir(const std::string& start)
    {
        std::error_code ec;
        std::filesystem::path p = start.empty() ? std::filesystem::current_path(ec) : std::filesystem::absolute(start, ec);
        p = p.lexically_normal();
        if (p.has_relative_path() && !p.has_filename()) p = p.parent_path();   // "a/b/" -> "a/b"
        if (ec || !std::filesystem::is_directory(p, ec)) p = std::filesystem::current_path(ec);
        dir_ = p;
    }

    // Rebuilds the visible rows and tells RUI the list and the dialog changed.
    void Refresh()
    {
        dirText_ = dir_.generic_string();
        rows_.clear();
        items_.clear();

        std::vector<std::filesystem::path> dirs, files;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir_, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
        {
            const std::string filename = it->path().filename().string();
            if (filename.empty() || filename.front() == '.') continue;
            std::error_code entryEc;
            if (it->is_directory(entryEc)) dirs.push_back(it->path());
            else if (it->is_regular_file(entryEc) && Matches(it->path())) files.push_back(it->path());
        }

        const auto byName = [](const std::filesystem::path& a, const std::filesystem::path& b)
        { return Lower(a.filename().string()) < Lower(b.filename().string()); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const std::filesystem::path& d : dirs)
        {
            rows_.push_back({static_cast<int>(items_.size()), "[dir] " + d.filename().string()});
            items_.push_back({d, true});
        }
        for (const std::filesystem::path& f : files)
        {
            rows_.push_back({static_cast<int>(items_.size()), f.filename().string()});
            items_.push_back({f, false});
        }

        if (ec) status_ = "Can't read folder: " + ec.message();

        ui_.runtime.Invalidate("FileEntries");
        ui_.RequestRebuild();
    }

    void Close()
    {
        open_ = 0;
        ui_.RequestRebuild();
    }

    void Finish(const std::filesystem::path& path)
    {
        Callback cb = std::move(cb_);
        Close();
        if (cb) cb(ProjectPath(path));
    }

    void Click(int index)
    {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        const Item item = items_[static_cast<std::size_t>(index)];
        overwriteArmed_.clear();
        if (item.isDir)
        {
            dir_ = item.path;
            selected_.clear();
            status_.clear();
            Refresh();
            return;
        }
        selected_ = item.path;
        status_ = "Selected: " + item.path.filename().string();
        if (save_ == 1) name_ = item.path.filename().string();
        ui_.RequestRebuild();
    }

    void Act(int action)
    {
        switch (action)
        {
        case kCancel:
            Close();
            break;

        case kUp:
        {
            const std::filesystem::path parent = dir_.parent_path();
            if (!parent.empty() && parent != dir_)
            {
                dir_ = parent;
                selected_.clear();
                status_.clear();
                overwriteArmed_.clear();
                Refresh();
            }
            break;
        }

        case kConfirm:
            Confirm();
            break;

        default:
            break;
        }
    }

    void Confirm()
    {
        if (save_ != 1)
        {
            if (selected_.empty() || !Matches(selected_))
            {
                status_ = "Pick a file first";
                ui_.RequestRebuild();
                return;
            }
            Finish(selected_);
            return;
        }

        std::string name = Trim(name_);
        if (name.empty())
        {
            status_ = "Enter a file name";
            ui_.RequestRebuild();
            return;
        }
        if (name.find_first_of("/\\") != std::string::npos)
        {
            status_ = "A name can't contain / or \\";
            ui_.RequestRebuild();
            return;
        }
        if (!ext_.empty() && Lower(std::filesystem::path(name).extension().string()) != ext_) name += ext_;
        name_ = name;

        const std::filesystem::path target = dir_ / name;
        std::error_code ec;
        if (std::filesystem::exists(target, ec) && overwriteArmed_ != target.string())
        {
            overwriteArmed_ = target.string();
            status_ = name + " exists - press Save again to overwrite";
            ui_.RequestRebuild();
            return;
        }
        Finish(target);
    }

    rui::Context& ui_;

    // Bound to the markup. Plain ints (not bool) because the markup writes the
    // action/click ones with `fd_action = 1` and reads the others with `== 1`.
    int open_ = 0;
    int save_ = 0;
    int clicked_ = -1;
    int action_ = 0;
    std::string title_;
    std::string dirText_;
    std::string name_;
    std::string status_;

    std::vector<Entry> rows_;
    std::vector<Item> items_;
    std::filesystem::path dir_;
    std::filesystem::path selected_;
    std::string ext_;
    std::string overwriteArmed_;
    Callback cb_;
};