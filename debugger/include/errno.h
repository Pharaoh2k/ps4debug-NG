// SPDX-License-Identifier: GPL-3.0-only

#ifndef _ERRNO_H
#define _ERRNO_H

#define	EPERM		1
#define	ENOENT		2
#define	ESRCH		3
#define	EINTR		4
#define	EIO		5
#define	ENXIO		6
#define	E2BIG		7
#define	ENOEXEC		8
#define	EBADF		9
#define	ECHILD		10
#define	EDEADLK		11

#define	ENOMEM		12
#define	EACCES		13
#define	EFAULT		14
#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	ENOTBLK		15
#endif
#define	EBUSY		16
#define	EEXIST		17
#define	EXDEV		18
#define	ENODEV		19
#define	ENOTDIR		20
#define	EISDIR		21
#define	EINVAL		22
#define	ENFILE		23
#define	EMFILE		24
#define	ENOTTY		25
#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	ETXTBSY		26
#endif
#define	EFBIG		27
#define	ENOSPC		28
#define	ESPIPE		29
#define	EROFS		30
#define	EMLINK		31
#define	EPIPE		32

#define	EDOM		33
#define	ERANGE		34

#define	EAGAIN		35
#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	EWOULDBLOCK	EAGAIN
#define	EINPROGRESS	36
#define	EALREADY	37

#define	ENOTSOCK	38
#define	EDESTADDRREQ	39
#define	EMSGSIZE	40
#define	EPROTOTYPE	41
#define	ENOPROTOOPT	42
#define	EPROTONOSUPPORT	43
#define	ESOCKTNOSUPPORT	44
#define	EOPNOTSUPP	45
#define	ENOTSUP		EOPNOTSUPP
#define	EPFNOSUPPORT	46
#define	EAFNOSUPPORT	47
#define	EADDRINUSE	48
#define	EADDRNOTAVAIL	49

#define	ENETDOWN	50
#define	ENETUNREACH	51
#define	ENETRESET	52
#define	ECONNABORTED	53
#define	ECONNRESET	54
#define	ENOBUFS		55
#define	EISCONN		56
#define	ENOTCONN	57
#define	ESHUTDOWN	58
#define	ETOOMANYREFS	59
#define	ETIMEDOUT	60
#define	ECONNREFUSED	61

#define	ELOOP		62
#endif
#define	ENAMETOOLONG	63

#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	EHOSTDOWN	64
#define	EHOSTUNREACH	65
#endif
#define	ENOTEMPTY	66

#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	EPROCLIM	67
#define	EUSERS		68
#define	EDQUOT		69

#define	ESTALE		70
#define	EREMOTE		71
#define	EBADRPC		72
#define	ERPCMISMATCH	73
#define	EPROGUNAVAIL	74
#define	EPROGMISMATCH	75
#define	EPROCUNAVAIL	76
#endif

#define	ENOLCK		77
#define	ENOSYS		78

#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	EFTYPE		79
#define	EAUTH		80
#define	ENEEDAUTH	81
#define	EIDRM		82
#define	ENOMSG		83
#define	EOVERFLOW	84
#define	ECANCELED	85
#define	EILSEQ		86
#define	ENOATTR		87

#define	EDOOFUS		88
#endif

#define	EBADMSG		89
#define	EMULTIHOP	90
#define	ENOLINK		91
#define	EPROTO		92

#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	ENOTCAPABLE	93
#define	ECAPMODE	94
#define	ENOTRECOVERABLE	95
#define	EOWNERDEAD	96
#endif

#ifndef PS4DEBUG_ERRNO_POSIX_SOURCE
#define	ELAST		96
#endif

/*
 * Orbis-specific socket error returned by sceNetAccept() when the listen
 * socket has been aborted (e.g. via sceNetSocketAbort). No FreeBSD errno
 * name exists for it; the numeric value is what the kernel returns.
 */
#define	EACCEPTABORT	163

#define	ERESTART	(-1)
#define	EJUSTRETURN	(-2)
#define	ENOIOCTL	(-3)
#define	EDIRIOCTL	(-4)
#define	ERELOOKUP	(-5)

#endif
