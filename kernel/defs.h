// defs.h - Function declarations shared across the Lab 2 kernel bootstrap.

// string.c
int      strcmp(const char *, const char *);
char*    strcpy(char *, const char *);
char*    strncpy(char *, const char *, int);
char*    safestrcpy(char *, const char *, int);
int      strlen(const char *);
void*    memset(void *, int, uint);
void*    memmove(void *, const void *, uint);
int      memcmp(const void *, const void *, uint);
void*    memcpy(void *, const void *, uint);

// boot.c
const char* boot_banner(void);
void        publish_boot_banner(void);

// start.c
void     start(void);

// main.c
void     main(void);