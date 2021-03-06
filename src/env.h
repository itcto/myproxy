
#ifndef __ENV_H__
#define __ENV_H__

#include "mp_cfg.h"
#include "ctnr_impl.h"
#include "hook.h"

namespace GLOBAL_ENV {

  /* the payload size */
  constexpr int MAX_PAYLOAD = 1600;

  /* the config file item */
  extern mp_cfg m_conf ;

  /* the table list */
  extern safeTableDetailList m_tables;

  /* the myfd -> physical statement id mappings */
  extern safeMyFdMapList m_mfMaps ;

#define likely(x) __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
} ;


#endif /* __ENV_H__*/
