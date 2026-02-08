#include "editor_application.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    mmo::editor::EditorApplication editor;

    if (!editor.init()) {
        std::cerr << "Failed to initialize editor" << std::endl;
        return 1;
    }

    editor.run();
    editor.shutdown();

    return 0;
}
