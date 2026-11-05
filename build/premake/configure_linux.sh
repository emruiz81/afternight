echo ****************Building Clang makefiles****************
../../tools/premake5 --cc=clang --os=linux gmake || exit 1
touch ../make-linux/.premake-generated
echo
echo
echo **********Building Codelite-clang project files***********
../../tools/premake5 --cc=clang --os=linux codelite || exit 1
touch ../codelite-linux/.premake-generated
