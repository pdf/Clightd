#include <xf86drm.h>
#include <xf86drmMode.h>

void take_control(void);
void release_control(void);
drmModeConnectorPtr get_active_connector(int fd, int connector_id);
drmModePropertyPtr drm_get_prop(int fd, drmModeConnectorPtr connector, const char *name);
