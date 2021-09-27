#ifndef CONFIG_H
#define CONFIG_H

#define HAVE_CHECK_MD5

#ifndef HAVE_SAVE_MD5
#define HAVE_SAVE_MD5 0
#endif

#ifndef MS_PER_TICK
#define MS_PER_TICK (1000) /* 单位是毫秒 */
#endif

#endif  /* CONFIG_H */
