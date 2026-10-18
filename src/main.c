#include "remember_app.h"

#ifdef REMEMBER_TEST_VFS
#include "test_vfs.h"
#endif

int main(int argc, char **argv)
{
#ifdef REMEMBER_TEST_VFS
    remember_test_vfs_install();
#endif
    return remember_main(argc, argv);
}
