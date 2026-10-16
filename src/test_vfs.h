#ifndef REMEMBER_TEST_VFS_H
#define REMEMBER_TEST_VFS_H

/*
 * Test-only VFS (X-E02). The remember CLI calls this once at startup when
 * REMEMBER_TEST_VFS is set. remember_plain, remember_core, and Release do not.
 */
void remember_test_vfs_install(void);

#endif /* REMEMBER_TEST_VFS_H */
