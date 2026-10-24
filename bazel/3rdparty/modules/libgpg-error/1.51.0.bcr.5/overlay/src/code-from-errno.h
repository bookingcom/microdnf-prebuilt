/* Generated code-from-errno.h */
#include <errno.h>
static const int err_code_from_index[] = {
  GPG_ERR_E2BIG,
  GPG_ERR_EACCES,
  GPG_ERR_EADDRINUSE,
  GPG_ERR_EADDRNOTAVAIL,
  GPG_ERR_EADV,
  GPG_ERR_EAFNOSUPPORT,
  GPG_ERR_EAGAIN,
  GPG_ERR_EALREADY,
  GPG_ERR_EAUTH,
  GPG_ERR_EBACKGROUND,
  GPG_ERR_EBADE,
  GPG_ERR_EBADF,
  GPG_ERR_EBADFD,
  GPG_ERR_EBADMSG,
  GPG_ERR_EBADR,
  GPG_ERR_EBADRPC,
  GPG_ERR_EBADRQC,
  GPG_ERR_EBADSLT,
  GPG_ERR_EBFONT,
  GPG_ERR_EBUSY,
  GPG_ERR_ECANCELED,
  GPG_ERR_ECHILD,
  GPG_ERR_ECHRNG,
  GPG_ERR_ECOMM,
  GPG_ERR_ECONNABORTED,
  GPG_ERR_ECONNREFUSED,
  GPG_ERR_ECONNRESET,
  GPG_ERR_ED,
  GPG_ERR_EDEADLK,
  GPG_ERR_EDEADLOCK,
  GPG_ERR_EDESTADDRREQ,
  GPG_ERR_EDIED,
  GPG_ERR_EDOM,
  GPG_ERR_EDOTDOT,
  GPG_ERR_EDQUOT,
  GPG_ERR_EEXIST,
  GPG_ERR_EFAULT,
  GPG_ERR_EFBIG,
  GPG_ERR_EFTYPE,
  GPG_ERR_EGRATUITOUS,
  GPG_ERR_EGREGIOUS,
  GPG_ERR_EHOSTDOWN,
  GPG_ERR_EHOSTUNREACH,
  GPG_ERR_EIDRM,
  GPG_ERR_EIEIO,
  GPG_ERR_EILSEQ,
  GPG_ERR_EINPROGRESS,
  GPG_ERR_EINTR,
  GPG_ERR_EINVAL,
  GPG_ERR_EIO,
  GPG_ERR_EISCONN,
  GPG_ERR_EISDIR,
  GPG_ERR_EISNAM,
  GPG_ERR_EL2HLT,
  GPG_ERR_EL2NSYNC,
  GPG_ERR_EL3HLT,
  GPG_ERR_EL3RST,
  GPG_ERR_ELIBACC,
  GPG_ERR_ELIBBAD,
  GPG_ERR_ELIBEXEC,
  GPG_ERR_ELIBMAX,
  GPG_ERR_ELIBSCN,
  GPG_ERR_ELNRNG,
  GPG_ERR_ELOOP,
  GPG_ERR_EMEDIUMTYPE,
  GPG_ERR_EMFILE,
  GPG_ERR_EMLINK,
  GPG_ERR_EMSGSIZE,
  GPG_ERR_EMULTIHOP,
  GPG_ERR_ENAMETOOLONG,
  GPG_ERR_ENAVAIL,
  GPG_ERR_ENEEDAUTH,
  GPG_ERR_ENETDOWN,
  GPG_ERR_ENETRESET,
  GPG_ERR_ENETUNREACH,
  GPG_ERR_ENFILE,
  GPG_ERR_ENOANO,
  GPG_ERR_ENOBUFS,
  GPG_ERR_ENOCSI,
  GPG_ERR_ENODATA,
  GPG_ERR_ENODEV,
  GPG_ERR_ENOENT,
  GPG_ERR_ENOEXEC,
  GPG_ERR_ENOLCK,
  GPG_ERR_ENOLINK,
  GPG_ERR_ENOMEDIUM,
  GPG_ERR_ENOMEM,
  GPG_ERR_ENOMSG,
  GPG_ERR_ENONET,
  GPG_ERR_ENOPKG,
  GPG_ERR_ENOPROTOOPT,
  GPG_ERR_ENOSPC,
  GPG_ERR_ENOSR,
  GPG_ERR_ENOSTR,
  GPG_ERR_ENOSYS,
  GPG_ERR_ENOTBLK,
  GPG_ERR_ENOTCONN,
  GPG_ERR_ENOTDIR,
  GPG_ERR_ENOTEMPTY,
  GPG_ERR_ENOTNAM,
  GPG_ERR_ENOTSOCK,
  GPG_ERR_ENOTSUP,
  GPG_ERR_ENOTTY,
  GPG_ERR_ENOTUNIQ,
  GPG_ERR_ENXIO,
  GPG_ERR_EOPNOTSUPP,
  GPG_ERR_EOVERFLOW,
  GPG_ERR_EPERM,
  GPG_ERR_EPFNOSUPPORT,
  GPG_ERR_EPIPE,
  GPG_ERR_EPROCLIM,
  GPG_ERR_EPROCUNAVAIL,
  GPG_ERR_EPROGMISMATCH,
  GPG_ERR_EPROGUNAVAIL,
  GPG_ERR_EPROTO,
  GPG_ERR_EPROTONOSUPPORT,
  GPG_ERR_EPROTOTYPE,
  GPG_ERR_ERANGE,
  GPG_ERR_EREMCHG,
  GPG_ERR_EREMOTE,
  GPG_ERR_EREMOTEIO,
  GPG_ERR_ERESTART,
  GPG_ERR_EROFS,
  GPG_ERR_ERPCMISMATCH,
  GPG_ERR_ESHUTDOWN,
  GPG_ERR_ESOCKTNOSUPPORT,
  GPG_ERR_ESPIPE,
  GPG_ERR_ESRCH,
  GPG_ERR_ESRMNT,
  GPG_ERR_ESTALE,
  GPG_ERR_ESTRPIPE,
  GPG_ERR_ETIME,
  GPG_ERR_ETIMEDOUT,
  GPG_ERR_ETOOMANYREFS,
  GPG_ERR_ETXTBSY,
  GPG_ERR_EUCLEAN,
  GPG_ERR_EUNATCH,
  GPG_ERR_EUSERS,
  GPG_ERR_EWOULDBLOCK,
  GPG_ERR_EXDEV,
  GPG_ERR_EXFULL,
};
static inline int errno_to_idx(int code) {
#ifdef E2BIG
  if (code == E2BIG) return 0;
#endif
#ifdef EACCES
  if (code == EACCES) return 1;
#endif
#ifdef EADDRINUSE
  if (code == EADDRINUSE) return 2;
#endif
#ifdef EADDRNOTAVAIL
  if (code == EADDRNOTAVAIL) return 3;
#endif
#ifdef EADV
  if (code == EADV) return 4;
#endif
#ifdef EAFNOSUPPORT
  if (code == EAFNOSUPPORT) return 5;
#endif
#ifdef EAGAIN
  if (code == EAGAIN) return 6;
#endif
#ifdef EALREADY
  if (code == EALREADY) return 7;
#endif
#ifdef EAUTH
  if (code == EAUTH) return 8;
#endif
#ifdef EBACKGROUND
  if (code == EBACKGROUND) return 9;
#endif
#ifdef EBADE
  if (code == EBADE) return 10;
#endif
#ifdef EBADF
  if (code == EBADF) return 11;
#endif
#ifdef EBADFD
  if (code == EBADFD) return 12;
#endif
#ifdef EBADMSG
  if (code == EBADMSG) return 13;
#endif
#ifdef EBADR
  if (code == EBADR) return 14;
#endif
#ifdef EBADRPC
  if (code == EBADRPC) return 15;
#endif
#ifdef EBADRQC
  if (code == EBADRQC) return 16;
#endif
#ifdef EBADSLT
  if (code == EBADSLT) return 17;
#endif
#ifdef EBFONT
  if (code == EBFONT) return 18;
#endif
#ifdef EBUSY
  if (code == EBUSY) return 19;
#endif
#ifdef ECANCELED
  if (code == ECANCELED) return 20;
#endif
#ifdef ECHILD
  if (code == ECHILD) return 21;
#endif
#ifdef ECHRNG
  if (code == ECHRNG) return 22;
#endif
#ifdef ECOMM
  if (code == ECOMM) return 23;
#endif
#ifdef ECONNABORTED
  if (code == ECONNABORTED) return 24;
#endif
#ifdef ECONNREFUSED
  if (code == ECONNREFUSED) return 25;
#endif
#ifdef ECONNRESET
  if (code == ECONNRESET) return 26;
#endif
#ifdef ED
  if (code == ED) return 27;
#endif
#ifdef EDEADLK
  if (code == EDEADLK) return 28;
#endif
#ifdef EDEADLOCK
  if (code == EDEADLOCK) return 29;
#endif
#ifdef EDESTADDRREQ
  if (code == EDESTADDRREQ) return 30;
#endif
#ifdef EDIED
  if (code == EDIED) return 31;
#endif
#ifdef EDOM
  if (code == EDOM) return 32;
#endif
#ifdef EDOTDOT
  if (code == EDOTDOT) return 33;
#endif
#ifdef EDQUOT
  if (code == EDQUOT) return 34;
#endif
#ifdef EEXIST
  if (code == EEXIST) return 35;
#endif
#ifdef EFAULT
  if (code == EFAULT) return 36;
#endif
#ifdef EFBIG
  if (code == EFBIG) return 37;
#endif
#ifdef EFTYPE
  if (code == EFTYPE) return 38;
#endif
#ifdef EGRATUITOUS
  if (code == EGRATUITOUS) return 39;
#endif
#ifdef EGREGIOUS
  if (code == EGREGIOUS) return 40;
#endif
#ifdef EHOSTDOWN
  if (code == EHOSTDOWN) return 41;
#endif
#ifdef EHOSTUNREACH
  if (code == EHOSTUNREACH) return 42;
#endif
#ifdef EIDRM
  if (code == EIDRM) return 43;
#endif
#ifdef EIEIO
  if (code == EIEIO) return 44;
#endif
#ifdef EILSEQ
  if (code == EILSEQ) return 45;
#endif
#ifdef EINPROGRESS
  if (code == EINPROGRESS) return 46;
#endif
#ifdef EINTR
  if (code == EINTR) return 47;
#endif
#ifdef EINVAL
  if (code == EINVAL) return 48;
#endif
#ifdef EIO
  if (code == EIO) return 49;
#endif
#ifdef EISCONN
  if (code == EISCONN) return 50;
#endif
#ifdef EISDIR
  if (code == EISDIR) return 51;
#endif
#ifdef EISNAM
  if (code == EISNAM) return 52;
#endif
#ifdef EL2HLT
  if (code == EL2HLT) return 53;
#endif
#ifdef EL2NSYNC
  if (code == EL2NSYNC) return 54;
#endif
#ifdef EL3HLT
  if (code == EL3HLT) return 55;
#endif
#ifdef EL3RST
  if (code == EL3RST) return 56;
#endif
#ifdef ELIBACC
  if (code == ELIBACC) return 57;
#endif
#ifdef ELIBBAD
  if (code == ELIBBAD) return 58;
#endif
#ifdef ELIBEXEC
  if (code == ELIBEXEC) return 59;
#endif
#ifdef ELIBMAX
  if (code == ELIBMAX) return 60;
#endif
#ifdef ELIBSCN
  if (code == ELIBSCN) return 61;
#endif
#ifdef ELNRNG
  if (code == ELNRNG) return 62;
#endif
#ifdef ELOOP
  if (code == ELOOP) return 63;
#endif
#ifdef EMEDIUMTYPE
  if (code == EMEDIUMTYPE) return 64;
#endif
#ifdef EMFILE
  if (code == EMFILE) return 65;
#endif
#ifdef EMLINK
  if (code == EMLINK) return 66;
#endif
#ifdef EMSGSIZE
  if (code == EMSGSIZE) return 67;
#endif
#ifdef EMULTIHOP
  if (code == EMULTIHOP) return 68;
#endif
#ifdef ENAMETOOLONG
  if (code == ENAMETOOLONG) return 69;
#endif
#ifdef ENAVAIL
  if (code == ENAVAIL) return 70;
#endif
#ifdef ENEEDAUTH
  if (code == ENEEDAUTH) return 71;
#endif
#ifdef ENETDOWN
  if (code == ENETDOWN) return 72;
#endif
#ifdef ENETRESET
  if (code == ENETRESET) return 73;
#endif
#ifdef ENETUNREACH
  if (code == ENETUNREACH) return 74;
#endif
#ifdef ENFILE
  if (code == ENFILE) return 75;
#endif
#ifdef ENOANO
  if (code == ENOANO) return 76;
#endif
#ifdef ENOBUFS
  if (code == ENOBUFS) return 77;
#endif
#ifdef ENOCSI
  if (code == ENOCSI) return 78;
#endif
#ifdef ENODATA
  if (code == ENODATA) return 79;
#endif
#ifdef ENODEV
  if (code == ENODEV) return 80;
#endif
#ifdef ENOENT
  if (code == ENOENT) return 81;
#endif
#ifdef ENOEXEC
  if (code == ENOEXEC) return 82;
#endif
#ifdef ENOLCK
  if (code == ENOLCK) return 83;
#endif
#ifdef ENOLINK
  if (code == ENOLINK) return 84;
#endif
#ifdef ENOMEDIUM
  if (code == ENOMEDIUM) return 85;
#endif
#ifdef ENOMEM
  if (code == ENOMEM) return 86;
#endif
#ifdef ENOMSG
  if (code == ENOMSG) return 87;
#endif
#ifdef ENONET
  if (code == ENONET) return 88;
#endif
#ifdef ENOPKG
  if (code == ENOPKG) return 89;
#endif
#ifdef ENOPROTOOPT
  if (code == ENOPROTOOPT) return 90;
#endif
#ifdef ENOSPC
  if (code == ENOSPC) return 91;
#endif
#ifdef ENOSR
  if (code == ENOSR) return 92;
#endif
#ifdef ENOSTR
  if (code == ENOSTR) return 93;
#endif
#ifdef ENOSYS
  if (code == ENOSYS) return 94;
#endif
#ifdef ENOTBLK
  if (code == ENOTBLK) return 95;
#endif
#ifdef ENOTCONN
  if (code == ENOTCONN) return 96;
#endif
#ifdef ENOTDIR
  if (code == ENOTDIR) return 97;
#endif
#ifdef ENOTEMPTY
  if (code == ENOTEMPTY) return 98;
#endif
#ifdef ENOTNAM
  if (code == ENOTNAM) return 99;
#endif
#ifdef ENOTSOCK
  if (code == ENOTSOCK) return 100;
#endif
#ifdef ENOTSUP
  if (code == ENOTSUP) return 101;
#endif
#ifdef ENOTTY
  if (code == ENOTTY) return 102;
#endif
#ifdef ENOTUNIQ
  if (code == ENOTUNIQ) return 103;
#endif
#ifdef ENXIO
  if (code == ENXIO) return 104;
#endif
#ifdef EOPNOTSUPP
  if (code == EOPNOTSUPP) return 105;
#endif
#ifdef EOVERFLOW
  if (code == EOVERFLOW) return 106;
#endif
#ifdef EPERM
  if (code == EPERM) return 107;
#endif
#ifdef EPFNOSUPPORT
  if (code == EPFNOSUPPORT) return 108;
#endif
#ifdef EPIPE
  if (code == EPIPE) return 109;
#endif
#ifdef EPROCLIM
  if (code == EPROCLIM) return 110;
#endif
#ifdef EPROCUNAVAIL
  if (code == EPROCUNAVAIL) return 111;
#endif
#ifdef EPROGMISMATCH
  if (code == EPROGMISMATCH) return 112;
#endif
#ifdef EPROGUNAVAIL
  if (code == EPROGUNAVAIL) return 113;
#endif
#ifdef EPROTO
  if (code == EPROTO) return 114;
#endif
#ifdef EPROTONOSUPPORT
  if (code == EPROTONOSUPPORT) return 115;
#endif
#ifdef EPROTOTYPE
  if (code == EPROTOTYPE) return 116;
#endif
#ifdef ERANGE
  if (code == ERANGE) return 117;
#endif
#ifdef EREMCHG
  if (code == EREMCHG) return 118;
#endif
#ifdef EREMOTE
  if (code == EREMOTE) return 119;
#endif
#ifdef EREMOTEIO
  if (code == EREMOTEIO) return 120;
#endif
#ifdef ERESTART
  if (code == ERESTART) return 121;
#endif
#ifdef EROFS
  if (code == EROFS) return 122;
#endif
#ifdef ERPCMISMATCH
  if (code == ERPCMISMATCH) return 123;
#endif
#ifdef ESHUTDOWN
  if (code == ESHUTDOWN) return 124;
#endif
#ifdef ESOCKTNOSUPPORT
  if (code == ESOCKTNOSUPPORT) return 125;
#endif
#ifdef ESPIPE
  if (code == ESPIPE) return 126;
#endif
#ifdef ESRCH
  if (code == ESRCH) return 127;
#endif
#ifdef ESRMNT
  if (code == ESRMNT) return 128;
#endif
#ifdef ESTALE
  if (code == ESTALE) return 129;
#endif
#ifdef ESTRPIPE
  if (code == ESTRPIPE) return 130;
#endif
#ifdef ETIME
  if (code == ETIME) return 131;
#endif
#ifdef ETIMEDOUT
  if (code == ETIMEDOUT) return 132;
#endif
#ifdef ETOOMANYREFS
  if (code == ETOOMANYREFS) return 133;
#endif
#ifdef ETXTBSY
  if (code == ETXTBSY) return 134;
#endif
#ifdef EUCLEAN
  if (code == EUCLEAN) return 135;
#endif
#ifdef EUNATCH
  if (code == EUNATCH) return 136;
#endif
#ifdef EUSERS
  if (code == EUSERS) return 137;
#endif
#ifdef EWOULDBLOCK
  if (code == EWOULDBLOCK) return 138;
#endif
#ifdef EXDEV
  if (code == EXDEV) return 139;
#endif
#ifdef EXFULL
  if (code == EXFULL) return 140;
#endif
  return -1;
}
