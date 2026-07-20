
#ifndef ZECS_EXPORT_H
#define ZECS_EXPORT_H

#ifdef ZECS_STATIC_DEFINE
#  define ZECS_EXPORT
#  define ZECS_NO_EXPORT
#else
#  ifndef ZECS_EXPORT
#    ifdef zecs_EXPORTS
        /* We are building this library */
#      define ZECS_EXPORT 
#    else
        /* We are using this library */
#      define ZECS_EXPORT 
#    endif
#  endif

#  ifndef ZECS_NO_EXPORT
#    define ZECS_NO_EXPORT 
#  endif
#endif

#ifndef ZECS_DEPRECATED
#  define ZECS_DEPRECATED __declspec(deprecated)
#endif

#ifndef ZECS_DEPRECATED_EXPORT
#  define ZECS_DEPRECATED_EXPORT ZECS_EXPORT ZECS_DEPRECATED
#endif

#ifndef ZECS_DEPRECATED_NO_EXPORT
#  define ZECS_DEPRECATED_NO_EXPORT ZECS_NO_EXPORT ZECS_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef ZECS_NO_DEPRECATED
#    define ZECS_NO_DEPRECATED
#  endif
#endif

#endif /* ZECS_EXPORT_H */
