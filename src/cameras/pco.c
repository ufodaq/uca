
#include <stdlib.h>
#include <string.h>
#include <libpco/libpco.h>
#include "uca.h"
#include "uca-cam.h"
#include "uca-grabber.h"
#include "pco.h"

/* TODO: REMOVE THIS ASAP */
#include <fgrab_struct.h>

#define GET_PCO(uca) ((struct pco_edge_t *)(uca->user))

#define set_void(p, type, value) { *((type *) p) = (type) value; }


static uint32_t uca_pco_set_bitdepth(struct uca_camera_t *cam, uint8_t *bitdepth)
{
    /* TODO: it's not possible via CameraLink so do it via frame grabber */
    return 0;
}

static uint32_t uca_pco_set_exposure(struct uca_camera_t *cam, uint32_t *exposure)
{
    uint32_t e, d;
    if (pco_get_delay_exposure(GET_PCO(cam), &d, &e) != PCO_NOERROR)
        return UCA_ERR_PROP_GENERAL;
    if (pco_set_delay_exposure(GET_PCO(cam), d, *exposure) != PCO_NOERROR)
        return UCA_ERR_PROP_GENERAL;
    return UCA_NO_ERROR;
}

static uint32_t uca_pco_set_delay(struct uca_camera_t *cam, uint32_t *delay)
{
    uint32_t e, d;
    if (pco_get_delay_exposure(GET_PCO(cam), &d, &e) != PCO_NOERROR)
        return UCA_ERR_PROP_GENERAL;
    if (pco_set_delay_exposure(GET_PCO(cam), *delay, e) != PCO_NOERROR)
        return UCA_ERR_PROP_GENERAL;
    return UCA_NO_ERROR;
}

static uint32_t uca_pco_acquire_image(struct uca_camera_t *cam, void *buffer)
{
    return UCA_NO_ERROR;
}

static uint32_t uca_pco_destroy(struct uca_camera_t *cam)
{
    pco_set_rec_state(GET_PCO(cam), 0);
    pco_destroy(GET_PCO(cam));
    return UCA_NO_ERROR;
}

static uint32_t uca_pco_set_property(struct uca_camera_t *cam, enum uca_property_ids property, void *data)
{
    struct uca_grabber_t *grabber = cam->grabber;

    switch (property) {
        case UCA_PROP_WIDTH:
            if (grabber->set_property(grabber, FG_WIDTH, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            cam->frame_width = *((uint32_t *) data);
            break;

        case UCA_PROP_HEIGHT:
            if (grabber->set_property(grabber, FG_HEIGHT, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            cam->frame_height = *((uint32_t *) data);
            break;

        case UCA_PROP_X_OFFSET:
            if (grabber->set_property(grabber, FG_XOFFSET, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            break;

        case UCA_PROP_Y_OFFSET:
            if (grabber->set_property(grabber, FG_YOFFSET, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_VALUE_OUT_OF_RANGE;
            break;

        case UCA_PROP_EXPOSURE:
            return uca_pco_set_exposure(cam, (uint32_t *) data);

        case UCA_PROP_DELAY:
            return uca_pco_set_delay(cam, (uint32_t *) data);

        case UCA_PROP_TIMESTAMP_MODE:
            return pco_set_timestamp_mode(GET_PCO(cam), *((uint16_t *) data));

        default:
            return UCA_ERR_PROP_INVALID;
    }
    return UCA_NO_ERROR;
}


static uint32_t uca_pco_get_property(struct uca_camera_t *cam, enum uca_property_ids property, void *data)
{
    struct pco_edge_t *pco = GET_PCO(cam);
    struct uca_grabber_t *grabber = cam->grabber;

    switch (property) {
        case UCA_PROP_NAME: 
            {
                /* FIXME: how to ensure, that buffer is large enough? */
                SC2_Camera_Name_Response name;

                /* FIXME: This is _not_ a mistake. For some reason (which I
                 * still have to figure out), it is sometimes not possible to
                 * read the camera name... unless the same call precedes that
                 * one.*/
                pco_read_property(pco, GET_CAMERA_NAME, &name, sizeof(name));
                pco_read_property(pco, GET_CAMERA_NAME, &name, sizeof(name));
                strcpy((char *) data, name.szName);
            }
            break;

        case UCA_PROP_TEMPERATURE_SENSOR:
            {
                SC2_Temperature_Response temperature;
                if (pco_read_property(pco, GET_TEMPERATURE, &temperature, sizeof(temperature)) == PCO_NOERROR)
                    set_void(data, uint32_t, temperature.sCCDtemp / 10);
            }
            break;

        case UCA_PROP_TEMPERATURE_CAMERA:
            {
                SC2_Temperature_Response temperature;
                if (pco_read_property(pco, GET_TEMPERATURE, &temperature, sizeof(temperature)) == PCO_NOERROR)
                    set_void(data, uint32_t, temperature.sCamtemp);
            }
            break;

        case UCA_PROP_WIDTH:
            set_void(data, uint32_t, cam->frame_width);
            break;

        case UCA_PROP_WIDTH_MIN:
            set_void(data, uint32_t, 1);
            break;

        case UCA_PROP_WIDTH_MAX:
            set_void(data, uint32_t, pco->description.wMaxHorzResStdDESC);
            break;

        case UCA_PROP_HEIGHT:
            set_void(data, uint32_t, cam->frame_height);
            break;

        case UCA_PROP_HEIGHT_MIN:
            set_void(data, uint32_t, 1);
            break;

        case UCA_PROP_HEIGHT_MAX:
            set_void(data, uint32_t, pco->description.wMaxVertResStdDESC);
            break;

        case UCA_PROP_X_OFFSET:
            if (grabber->get_property(grabber, FG_XOFFSET, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_GENERAL;
            break;

        case UCA_PROP_Y_OFFSET:
            if (grabber->get_property(grabber, FG_YOFFSET, (uint32_t *) data) != UCA_NO_ERROR)
                return UCA_ERR_PROP_GENERAL;
            break;

        case UCA_PROP_DELAY:
            {
                uint32_t exposure;
                if (pco_get_delay_exposure(pco, (uint32_t *) data, &exposure) != PCO_NOERROR)
                    return UCA_ERR_PROP_INVALID;
            }
            break;

        case UCA_PROP_DELAY_MIN:
            set_void(data, uint32_t, pco->description.dwMinDelayDESC);
            break;

        case UCA_PROP_DELAY_MAX:
            set_void(data, uint32_t, pco->description.dwMaxDelayDESC);
            break;

        case UCA_PROP_EXPOSURE:
            {
                uint32_t delay;
                if (pco_get_delay_exposure(pco, &delay, (uint32_t *) data) != PCO_NOERROR)
                    return UCA_ERR_PROP_INVALID;
            }
            break;

        case UCA_PROP_EXPOSURE_MIN:
            set_void(data, uint32_t, pco->description.dwMinExposureDESC);
            break;

        case UCA_PROP_EXPOSURE_MAX:
            set_void(data, uint32_t, pco->description.dwMaxExposureDESC);
            break;

        case UCA_PROP_BITDEPTH:
            set_void(data, uint32_t, 16);
            break;

        default:
            return UCA_ERR_PROP_INVALID;
    }
    return UCA_NO_ERROR;
}

uint32_t uca_pco_start_recording(struct uca_camera_t *cam)
{
    struct pco_edge_t *pco = GET_PCO(cam);
    if (pco_arm_camera(pco) != PCO_NOERROR)
        return UCA_ERR_CAM_ARM;
    if (pco_set_rec_state(pco, 1) != PCO_NOERROR)
        return UCA_ERR_CAM_RECORD;
    return cam->grabber->acquire(cam->grabber, -1);
}

uint32_t uca_pco_stop_recording(struct uca_camera_t *cam)
{
    if (pco_set_rec_state(GET_PCO(cam), 0) != PCO_NOERROR)
        return UCA_ERR_PROP_GENERAL;
}

uint32_t uca_pco_grab(struct uca_camera_t *cam, char *buffer)
{
    uint16_t *frame;
    uint32_t err = cam->grabber->grab(cam->grabber, (void **) &frame);
    if (err != UCA_NO_ERROR)
        return err;
    /* FIXME: choose according to data format */
    //pco_reorder_image_5x16((uint16_t *) buffer, frame, cam->frame_width, cam->frame_height);
    memcpy(buffer, frame, cam->frame_width*cam->frame_height*2);
    return UCA_NO_ERROR;
}

uint32_t uca_pco_init(struct uca_camera_t **cam, struct uca_grabber_t *grabber)
{
    struct pco_edge_t *pco = pco_init();

    if (pco == NULL) {
        return UCA_ERR_CAM_NOT_FOUND;
    }

    if ((pco->serial_ref == NULL) || !pco_is_active(pco)) {
        pco_destroy(pco);
        return UCA_ERR_CAM_NOT_FOUND;
    }

    struct uca_camera_t *uca = (struct uca_camera_t *) malloc(sizeof(struct uca_camera_t));
    uca->user = pco;
    uca->grabber = grabber;
    uca->grabber->asynchronous = true;

    /* Camera found, set function pointers... */
    uca->destroy = &uca_pco_destroy;
    uca->set_property = &uca_pco_set_property;
    uca->get_property = &uca_pco_get_property;
    uca->start_recording = &uca_pco_start_recording;
    uca->stop_recording = &uca_pco_stop_recording;
    uca->grab = &uca_pco_grab;

    /* Prepare camera for recording */
    pco_set_scan_mode(pco, PCO_SCANMODE_SLOW);
    pco_set_rec_state(pco, 0);
    pco_set_timestamp_mode(pco, 2);
    pco_set_timebase(pco, 1, 1); 
    pco_arm_camera(pco);

    /* Prepare frame grabber for recording */
    int val = FG_CL_8BIT_FULL_10;
    grabber->set_property(grabber, FG_CAMERA_LINK_CAMTYP, &val);

    val = FG_GRAY;
    grabber->set_property(grabber, FG_FORMAT, &val);

    val = FREE_RUN;
    grabber->set_property(grabber, FG_TRIGGERMODE, &val);

    int width, height;
    pco_get_actual_size(pco, &width, &height);
    uca->frame_width = width;
    uca->frame_height = height;

    /* Yes, we really have to take an image twice as large because we set the
     * CameraLink interface to 8-bit 10 Taps, but are actually using 5x16 bits. */
    width *= 2;
    grabber->set_property(grabber, FG_WIDTH, &width);
    grabber->set_property(grabber, FG_HEIGHT, &height);

    uca->state = UCA_CAM_CONFIGURABLE;
    *cam = uca;

    return UCA_NO_ERROR;
}
