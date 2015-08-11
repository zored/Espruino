#include "platform_config.h"
#include "jsinteractive.h"
#include "jshardware.h"

int main() {
  jshInit();
  jsvInit();
  jsiInit(true);
  while (1) {
    jsiLoop();
}

  // js*Kill()
}
