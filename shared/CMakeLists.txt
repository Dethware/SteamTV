# Code shared by the shell and the programs it shows (apps/webapp). Each folder is one QML
# module, built as a static library, like the shell's topics (shell/DEVELOPMENT.md, Layout).
# A program takes it in with
#
#     add_subdirectory(<path to this folder> shared)
#
# so its build.sh mounts the whole repository, not only its own folder. See DEVELOPMENT.md.

add_subdirectory(input)
