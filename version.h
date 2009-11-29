#ifndef PURPLE_VERSION_H
#define PURPLE_VERSION_H

#define PURPLE_MAJOR_VERSION (2)
#define PURPLE_MINOR_VERSION (6)
#define PURPLE_MICRO_VERSION (0)

#define PURPLE_VERSION_CHECK(x,y,z) ((x) == PURPLE_MAJOR_VERSION && \
                                     ((y) < PURPLE_MINOR_VERSION || \
                                      ((y) == PURPLE_MINOR_VERSION && (z) <= PURPLE_MICRO_VERSION)))

#ifdef __cplusplus
extern "C" {
#endif

const char *purple_version_check (guint required_major, guint required_minor, guint required_micro);
extern const guint purple_major_version;
extern const guint purple_minor_version;
extern const guint purple_micro_version;

#ifdef __cplusplus
}
#endif

#endif /* PURPLE_VERSION_H */
