#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <utility>

using namespace std;

class FileSystemError : public runtime_error {
public:
    explicit FileSystemError(const string& msg) : runtime_error(msg) {}
};

namespace detail {
    inline void validateName(const string& name) {
        if (name.empty()) throw invalid_argument("Name must not be empty");
        if (name.find('/') != string::npos) throw invalid_argument("Name must not contain '/': " + name);
        if (name == "." || name == "..") throw invalid_argument("Name must not be '.' or '..'");
    }

    inline std::vector<std::string> parseSegments(const std::string& text) {
        std::vector<std::string> result;
        std::size_t pos = 0;
        if (!text.empty() && text[0] == '/') pos = 1;
        if (pos >= text.size()) return result;
        while (true) {
            std::size_t next = text.find('/', pos);
            std::string segment = text.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            validateName(segment);
            result.push_back(std::move(segment));
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        return result;
    }
}

class FileSystemElement {
protected:
    string name;
public:
    FileSystemElement(string current_name) : name(move(current_name)) { detail::validateName(name); }
    virtual ~FileSystemElement() = default;

    string getName() const { return name; }
    virtual size_t getSize() const = 0;
    virtual string toString() const = 0;
};

class File : public FileSystemElement {
private:
    string content;
public:
    File(string current_name, string current_content = "")
        : FileSystemElement(move(current_name)), content(move(current_content)) {
    }

    size_t getSize() const override { return content.size(); }

    string toString() const override {
        return getName() + " (file, " + to_string(getSize()) + " B)";
    }
};

class Directory : public FileSystemElement {
private:
    unordered_map<string, unique_ptr<FileSystemElement>> elements;
public:
    Directory(string current_name) : FileSystemElement(move(current_name)) {}

    size_t getSize() const override {
        size_t total = 0;
        for (const auto& pair : elements) total += pair.second->getSize();
        return total;
    }

    string toString() const override {
        return getName() + "/ (dir, " + to_string(getSize()) + " B, " + to_string(elements.size()) + " items)";
    }

    void addElement(unique_ptr<FileSystemElement> fse) {
        if (!fse) throw FileSystemError("Cannot add null element");
        if (elements.find(fse->getName()) != elements.end()) {
            throw FileSystemError("Element already exists: " + fse->getName());
        }
        elements.emplace(fse->getName(), move(fse));
    }

    unique_ptr<FileSystemElement> removeElement(const string& dname) {
        auto it = elements.find(dname);
        if (it == elements.end()) throw FileSystemError("Element not found: " + dname);
        auto result = move(it->second);
        elements.erase(it);
        return result;
    }

    void deleteElement(const string& dname) { removeElement(dname); }

    FileSystemElement* searchElement(const string& sname) const {
        auto it = elements.find(sname);
        return it == elements.end() ? nullptr : it->second.get();
    }

    void printTree(ostream& os = cout) const {
        printTree(0);
    }

private:
    void printTree(int indent = 0) const {
    string pad(indent, ' ');
    cout << pad << getName() << "/\n";

    for (const auto& [name_, child] : elements) {
        if (auto* dir = dynamic_cast<Directory*>(child.get())) {
            dir->printTree(indent + 2);
        } else {
            cout << pad << "  " << child->getName() << '\n';
        }
    }
}


public:

    bool isAncestorOf(const FileSystemElement* target) const {
        for (const auto& pair : elements) {
            if (pair.second.get() == target) return true;
            if (auto* dir = dynamic_cast<Directory*>(pair.second.get())) {
                if (dir->isAncestorOf(target)) return true;
            }
        }
        return false;
    }
};


class Path {
private:
    vector<string> segments_;
public:
    Path() = default;
    explicit Path(const string& text) : segments_(detail::parseSegments(text)) {}

    Path operator/(const Path& other) const {
        Path result(*this);
        result.segments_.insert(result.segments_.end(), other.segments_.begin(), other.segments_.end());
        return result;
    }

    Path operator/(const std::string& text) const { return *this / Path(text); }

    bool operator==(const Path& other) const { return segments_ == other.segments_; }
    bool operator!=(const Path& other) const { return !(*this == other); }

    const std::vector<std::string>& segments() const { return segments_; }
    bool isRoot() const { return segments_.empty(); }

    const string& name() const {
        if (isRoot()) throw std::logic_error("Root has no name");
        return segments_.back();
    }

    Path parent() const {
        if (isRoot()) throw std::logic_error("Root has no parent");
        Path result(*this);
        result.segments_.pop_back();
        return result;
    }

    string toString() const {
        if (isRoot()) return "/";
        string s;
        for (const auto& seg : segments_) s += "/" + seg;
        return s;
    }
};


class FileSystem {
private:
    unique_ptr<Directory> root;

    FileSystemElement* resolve(const Path& path) const {
        FileSystemElement* current = root.get();
        for (const auto& segment : path.segments()) {
            Directory* dir = dynamic_cast<Directory*>(current);
            if (dir == nullptr) return nullptr;
            current = dir->searchElement(segment);
            if (current == nullptr) return nullptr;
        }
        return current;
    }

    FileSystemElement& requireElement(const Path& path) const {
        FileSystemElement* element = resolve(path);
        if (element == nullptr) throw FileSystemError("Path not found: " + path.toString());
        return *element;
    }

    Directory& parentDirectory(const Path& path) const {
        if (path.isRoot()) throw FileSystemError("Root has no parent directory");
        FileSystemElement* parent = resolve(path.parent());
        if (parent == nullptr) throw FileSystemError("Parent not found: " + path.parent().toString());
        Directory* dir = dynamic_cast<Directory*>(parent);
        if (dir == nullptr) throw FileSystemError("Parent is not a directory: " + path.parent().toString());
        return *dir;
    }

public:
    FileSystem() : root(make_unique<Directory>("root")) {}

    FileSystemElement* find(const Path& path) const { return resolve(path); }

    void createFile(const Path& path, const string& content = "") {
        Directory& parent = parentDirectory(path);
        parent.addElement(make_unique<File>(path.name(), content));
    }

    void createDirectory(const Path& path) {
        Directory& parent = parentDirectory(path);
        parent.addElement(make_unique<Directory>(path.name()));
    }

    void remove(const Path& path) {
        if (path.isRoot()) throw FileSystemError("Cannot remove root");
        requireElement(path);
        parentDirectory(path).deleteElement(path.name());
    }

    void move(const Path& source, const Path& targetDir) {
        if (source.isRoot()) throw FileSystemError("Cannot move root");
        FileSystemElement& element = requireElement(source);
        Directory* target = dynamic_cast<Directory*>(&requireElement(targetDir));

        if (target == nullptr) throw FileSystemError("Target is not a directory");
        if (&element == target) throw FileSystemError("Cannot move into itself");

        const Directory* dir = dynamic_cast<const Directory*>(&element);
        if (dir != nullptr && dir->isAncestorOf(target)) {
            throw FileSystemError("Cannot move into descendant");
        }
        if (target->searchElement(element.getName()) != nullptr) {
            throw FileSystemError("Target already contains: " + element.getName());
        }

        Directory& parent = parentDirectory(source);
        target->addElement(parent.removeElement(source.name()));
    }

    size_t sizeOf(const Path& path) const { return requireElement(path).getSize(); }

    void printTree(ostream& os = cout) const { root->printTree(os); }
};


int main() {
    cout << "=== ТЕСТЫ ===" << endl;

    FileSystem fs;

    cout << "1. Создание директорий и файлов... ";
    fs.createDirectory(Path{ "home" });
    fs.createDirectory(Path{ "home/docs" });
    fs.createFile(Path{ "home/docs/report.txt" }, "0123456789");
    fs.createFile(Path{ "home/readme.txt" }, "abc");
    if (fs.find(Path{ "home/docs/report.txt" }) != nullptr) cout << "OK" << endl;
    else cout << "FAIL" << endl;

    cout << "2. Проверка размеров... ";
    if (fs.sizeOf(Path{ "home/docs" }) == 10 && fs.sizeOf(Path{ "home" }) == 13) cout << "OK" << endl;
    else cout << "FAIL" << endl;

    cout << "3. Ошибка: создание файла в несуществующей директории... ";
    try {
        fs.createFile(Path{ "no/parent/file.txt" });
        cout << "FAIL" << endl;
    }
    catch (const FileSystemError&) {
        cout << "OK" << endl;
    }

    cout << "4. Ошибка: создание файла с именем существующего... ";
    try {
        fs.createFile(Path{ "home/readme.txt" });
        cout << "FAIL" << endl;
    }
    catch (const FileSystemError&) {
        cout << "OK" << endl;
    }

    cout << "5. Ошибка: поиск несуществующего пути... ";
    try {
        fs.sizeOf(Path{ "missing/path" });
        cout << "FAIL" << endl;
    }
    catch (const FileSystemError&) {
        cout << "OK" << endl;
    }

    cout << "6. Ошибка: удаление корня... ";
    try {
        fs.remove(Path{});
        cout << "FAIL" << endl;
    }
    catch (const FileSystemError&) {
        cout << "OK" << endl;
    }

    cout << "7. Перемещение файла... ";
    fs.createDirectory(Path{ "archive" });
    fs.move(Path{ "home/docs/report.txt" }, Path{ "archive" });
    if (fs.find(Path{ "home/docs/report.txt" }) == nullptr && fs.find(Path{ "archive/report.txt" }) != nullptr) cout << "OK" << endl;
    else cout << "FAIL" << endl;

    cout << "8. Ошибка: перемещение директории в своего потомка... ";
    try {
        fs.move(Path{ "home" }, Path{ "home/docs" });
        cout << "FAIL" << endl;
    }
    catch (const FileSystemError&) {
        cout << "OK" << endl;
    }

    cout << "9. Удаление элемента... ";
    fs.remove(Path{ "home/readme.txt" });
    if (fs.find(Path{ "home/readme.txt" }) == nullptr) cout << "OK" << endl;
    else cout << "FAIL" << endl;

    cout << "10. Вывод дерева... " << endl;
    fs.printTree();
    cout << endl;

    cout << "Все тесты завершены." << endl;
    return 0;
}
