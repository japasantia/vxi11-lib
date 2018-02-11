#ifndef VXI11_GLOBAL_H
#define VXI11_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(VXI11_LIBRARY)
#  define VXI11SHARED_EXPORT Q_DECL_EXPORT
#else
#  define VXI11SHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // VXI11_GLOBAL_H
