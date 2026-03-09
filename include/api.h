#ifndef API_H
#define API_H

int api_start(int port);
void api_set_brightness(int val);
void api_stop(void);
int api_get_state(void);
int api_get_brightness(void);
void api_set_state(int on);

#endif
