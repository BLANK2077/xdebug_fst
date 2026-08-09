// Deliberately legacy DesignDB fixture: it has the version-1 lifecycle and
// resolution symbols but no ABI/capability discovery.
extern "C" {

void* xdd_init() { return reinterpret_cast<void*>(1); }
void xdd_close(void*) {}
int xdd_signal_count(void*) { return 1; }
int xdd_resolve(void*, const char*) { return 0; }

}
