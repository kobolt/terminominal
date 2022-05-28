#include <stdint.h>
#include <pthread.h>
#include "sdlgui.h"
#include "terminal.h"
#include "eia.h"

void *main_two(void *argp)
{
  (void)argp;
  while (1) {
    sdlgui_update();
  }
  return NULL;
}

int main(void)
{
  pthread_t tid;

  eia_init();
  terminal_init();
  sdlgui_init();

  pthread_create(&tid, NULL, main_two, NULL);
  while (1) {
    eia_update();
  }
  pthread_join(tid, NULL);

  return 0;
}

