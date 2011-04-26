
#include <stdio.h>
#include "uca.h"
#include "uca-cam.h"

int count_dots(const char *s)
{
    int res = 0;
    while (*(s++) != '\0')
        if (*s == '.')
            res++;
    return res;
}

void print_level(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("|  ");
    printf("|-- ");
}

int main(int argc, char *argv[])
{
    struct uca *u = uca_init(NULL);
    if (u == NULL) {
        printf("Couldn't find a camera\n");
        return 1;
    }

    /* take first camera */
    struct uca_camera *cam = u->cameras;

    const size_t num_bytes = 256;
    char string_value[num_bytes];
    uint32_t uint32_value;
    uint8_t uint8_value;

    while (cam != NULL) {
        for (int i = 0; i < UCA_PROP_LAST; i++) {
            struct uca_property *prop = uca_get_full_property(i);
            print_level(count_dots(prop->name));
            printf("%s = ", prop->name);
            switch (prop->type) {
                case uca_string:
                    if (uca_cam_get_property(cam, i, string_value, num_bytes) == UCA_NO_ERROR) {
                        printf("%s ", string_value);
                    }
                    else
                        printf("n/a");
                    break;
                case uca_uint32t:
                    if (uca_cam_get_property(cam, i, &uint32_value, 0) == UCA_NO_ERROR) {
                        printf("%i %s", uint32_value, uca_unit_map[prop->unit]);
                    }
                    else
                        printf("n/a");
                    break;
                case uca_uint8t:
                    if (uca_cam_get_property(cam, i, &uint8_value, 0) == UCA_NO_ERROR) {
                        printf("%i %s", uint8_value, uca_unit_map[prop->unit]);
                    }
                    else
                        printf("n/a");
                    break;
            }
            printf("\n");
        }
        cam = cam->next;
    }
    
    uca_destroy(u);
    return 0;
}
