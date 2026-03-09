#ifndef API_H
#define API_H

int api_start(int port);
void api_stop(void);
int api_wake_requested(void);
void api_set_state(int on);

#endif
