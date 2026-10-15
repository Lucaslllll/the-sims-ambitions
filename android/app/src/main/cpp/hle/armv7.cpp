// Runtime helpers additionally imported by the iOS armv7 executable.
#include "hle/common.h"
#include "objc/runtime.h"
#include <cmath>
#include <cstdlib>

#if defined(__arm__)
namespace {
int divsi(int a, int b) { return a / b; }
int modsi(int a, int b) { return a % b; }
unsigned udivsi(unsigned a, unsigned b) { return a / b; }
unsigned umodsi(unsigned a, unsigned b) { return a % b; }
int64_t divdi(int64_t a, int64_t b) { return a / b; }
int64_t fixdfdi(double v) { return (int64_t)v; }
int64_t fixsfdi(float v) { return (int64_t)v; }
double floatdidf(int64_t v) { return (double)v; }
float floatundisf(uint64_t v) { return (float)v; }
void unwind_register(void*) {}
void unwind_unregister(void*) {}
[[noreturn]] void unwind_resume(void*) { LOGE("ARMv7 exception resume"); abort(); }
id autorelease_return(id o) { return objc::autorelease(o); }
id retain_return(id o) { return objc::retain(o); }
id retain_autorelease_return(id o) { return objc::autorelease(objc::retain(o)); }
void store_strong(id* slot, id value) {
    id old = *slot;
    *slot = objc::retain(value);
    objc::release(old);
}
id get_property(id self, SEL, ptrdiff_t offset, BOOL atomic) {
    id value = *(id*)((char*)self + offset);
    return atomic ? objc::autorelease(objc::retain(value)) : value;
}
void set_property(id self, SEL, ptrdiff_t offset, id value, BOOL, BOOL copy) {
    id replacement = copy ? objc::send(value, SEL_("copy")) : objc::retain(value);
    id* slot = (id*)((char*)self + offset);
    id old = *slot;
    *slot = replacement;
    objc::release(old);
}
Class get_class(const char* name) { return objc::get_class(name); }
const char* class_name(Class cls) { return objc::class_name(cls); }
int sync_noop(id) { return 0; }
}
HLE_EXPORT_AS("___divsi3", divsi);
HLE_EXPORT_AS("___modsi3", modsi);
HLE_EXPORT_AS("___udivsi3", udivsi);
HLE_EXPORT_AS("___umodsi3", umodsi);
HLE_EXPORT_AS("___divdi3", divdi);
HLE_EXPORT_AS("___fixdfdi", fixdfdi);
HLE_EXPORT_AS("___fixsfdi", fixsfdi);
HLE_EXPORT_AS("___floatdidf", floatdidf);
HLE_EXPORT_AS("___floatundisf", floatundisf);
HLE_EXPORT_AS("__Unwind_SjLj_Register", unwind_register);
HLE_EXPORT_AS("__Unwind_SjLj_Unregister", unwind_unregister);
HLE_EXPORT_AS("__Unwind_SjLj_Resume", unwind_resume);
HLE_EXPORT(ceilf, (float(*)(float))ceilf);
HLE_EXPORT(floorf, (float(*)(float))floorf);
HLE_EXPORT(modf, (double(*)(double, double*))modf);
HLE_EXPORT(strncmp, strncmp);
HLE_EXPORT(objc_retain, objc::retain);
HLE_EXPORT(objc_release, objc::release);
HLE_EXPORT(objc_autoreleaseReturnValue, autorelease_return);
HLE_EXPORT(objc_retainAutoreleaseReturnValue, retain_autorelease_return);
HLE_EXPORT(objc_retainAutoreleasedReturnValue, retain_return);
HLE_EXPORT(objc_storeStrong, store_strong);
HLE_EXPORT(objc_getProperty, get_property);
HLE_EXPORT(objc_setProperty, set_property);
HLE_EXPORT(objc_getClass, get_class);
HLE_EXPORT(class_getName, class_name);
HLE_EXPORT(objc_sync_enter, sync_noop);
HLE_EXPORT(objc_sync_exit, sync_noop);
#endif
