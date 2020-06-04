//#include "RaspiStill-fixed.c"
#define MMAL_COMPONENT_USERDATA_T MMALCamera // NOt quite C++ but thats how its supposed to be used I think.

#include <stdio.h>
#include <mmal_logging.h>
#include <mmal_default_components.h>
#include <util/mmal_util.h>
#include <util/mmal_util_params.h>
#include <bcm_host.h>

#include "MMALCamera.h"

void c_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    MMALCamera *p = dynamic_cast<MMALCamera *>(port->component->userdata);
    p->callback(port, buffer);
}

MMALCamera::MMALCamera(int n) : cameraNum(n)
{

}

MMALCamera::~MMALCamera()
{

}

/**
 *  Buffer header callback function for camera still port
 *
 *  Callback will dump buffer data to the specific file
 *
 * @param port Pointer to port from which callback originated
 * @param buffer mmal buffer header pointer
 */
void MMALCamera::callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    if (port->type == MMAL_PORT_TYPE_OUTPUT)
    {
        int complete = 0;
        uint32_t bytes_written = buffer->length;

        if (buffer->length && file_handle)
        {
            mmal_buffer_header_mem_lock(buffer);

            bytes_written = fwrite(buffer->data, 1, buffer->length, file_handle);

            mmal_buffer_header_mem_unlock(buffer);
        }

        // We need to check we wrote what we wanted - it's possible we have run out of storage.
        if (bytes_written != buffer->length)
        {
            vcos_log_error("Unable to write buffer to file - aborting");
            complete = 1;
        }

        // Now flag if we have completed
        if (buffer->flags & (MMAL_BUFFER_HEADER_FLAG_EOS | MMAL_BUFFER_HEADER_FLAG_FRAME_END | MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED)) {
            complete = 1;
        }

        // release buffer back to the pool
        mmal_buffer_header_release(buffer);

        // and send one back to the port (if still open)
        if (port->is_enabled)
        {
            MMAL_STATUS_T status = MMAL_SUCCESS;
            MMAL_BUFFER_HEADER_T *new_buffer;

            new_buffer = mmal_queue_get(pool->queue);

            if (new_buffer)
            {
                status = mmal_port_send_buffer(port, new_buffer);
            }
            if (!new_buffer || status != MMAL_SUCCESS) {
                vcos_log_error("Unable to return a buffer to the camera port");
            }
        }
        else {
            fprintf(stderr, "callback: port not enabled\n");
        }

        if (complete) {
            vcos_semaphore_post(&(complete_semaphore));
        }

     }
}

/**
 * Create the camera component, set up its ports
 *
 * @return MMAL_SUCCESS if all OK, something else otherwise
 *
 */
void MMALCamera::create_camera_component()
{
    MMAL_ES_FORMAT_T *format = nullptr;
    MMAL_STATUS_T status = MMAL_EINVAL;
    MMAL_POOL_T *pool = nullptr;

    /* Create the component */
    try
    {
        status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);
        MMALException::throw_if(status != MMAL_SUCCESS, "Failed to create camera component");
        camera->userdata = this; // c_callback needs this to find this object.

        MMAL_PARAMETER_INT32_T camera_num = {{MMAL_PARAMETER_CAMERA_NUM, sizeof(camera_num)}, cameraNum};
        status = mmal_port_parameter_set(camera->control, &camera_num.hdr);
        MMALException::throw_if(status != MMAL_SUCCESS, "Could not select camera");
        MMALException::throw_if(camera->output_num == 0, "Camera doesn't have output ports");

        status = mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, 0);
        MMALException::throw_if(status != MMAL_SUCCESS, "Could not set sensor mode");


        // Enable the camera, and tell it its control callback function
        status = mmal_port_enable(camera->control, c_callback);
        MMALException::throw_if(status != MMAL_SUCCESS, "Unable to enable control port");

        //  set up the camera configuration
        {
            MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
                { MMAL_PARAMETER_CAMERA_CONFIG, sizeof(cam_config) },
                .max_stills_w = 4056, //FIXME: Get it dynamically instead.
                .max_stills_h = 3040,
                .stills_yuv422 = 1,
                .one_shot_stills = 1,
                .max_preview_video_w = 0,
                .max_preview_video_h = 0,
                .num_preview_video_frames = 0,
                .stills_capture_circular_buffer_height = 0,
                .fast_preview_resume = 0,
                .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
            };

            mmal_port_parameter_set(camera->control, &cam_config.hdr);
            fprintf(stderr, "Size set to %dx%d\n", cam_config.max_stills_w, cam_config.max_stills_h);
        }

    // FIXME:     raspicamcontrol_set_all_parameters(camera, &state->camera_parameters);

        // Now set up the port formats
        format = camera->port[CAPTURE_PORT]->format;
#if 0 // HACKMARK: Framerate???
        if(state->camera_parameters.shutter_speed > 6000000)
        {
            MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                    { 5, 1000 }, {166, 1000}
                                                   };
            mmal_port_parameter_set(still_port, &fps_range.hdr);
        }
        else if(state->camera_parameters.shutter_speed > 1000000)
        {
            MMAL_PARAMETER_FPS_RANGE_T fps_range = {{MMAL_PARAMETER_FPS_RANGE, sizeof(fps_range)},
                                                    { 167, 1000 }, {999, 1000}
                                                   };
            mmal_port_parameter_set(camera->port[CAPTURE_PORT], &fps_range.hdr);
        }
#endif

        // Set our stills format on the stills (for encoder) port
        format->encoding = MMAL_ENCODING_OPAQUE;
        format->encoding_variant = MMAL_ENCODING_I420;
        format->es->video.width = 32;
        format->es->video.height = 16;
        format->es->video.crop.x = 0;
        format->es->video.crop.y = 0;
        format->es->video.crop.width = 32;
        format->es->video.crop.height = 16;
        format->es->video.frame_rate.num = 0;
        format->es->video.frame_rate.den = 1;

        status = mmal_port_format_commit(camera->port[CAPTURE_PORT]);
        MMALException::throw_if(status != MMAL_SUCCESS, "camera still format couldn't be set");

        camera->port[CAPTURE_PORT]->buffer_size = camera->port[CAPTURE_PORT]->buffer_size_recommended;

        /* Ensure there are enough buffers to avoid dropping frames */
        if (camera->port[CAPTURE_PORT]->buffer_num < 3) {
            camera->port[CAPTURE_PORT]->buffer_num = 3;
        }

        /* Create pool of buffer headers for the output port to consume */
        pool = mmal_port_pool_create(camera->port[CAPTURE_PORT], camera->port[CAPTURE_PORT]->buffer_num, camera->port[CAPTURE_PORT]->buffer_size);
        MMALException::throw_if(!pool, "Failed to create buffer header pool for camera output port");
    }
    catch(MMALException &e)
    {
        if (camera) {
            mmal_component_destroy(camera);
            camera = nullptr;
        }
        throw(e);
    }
}

/**
 * Main exposure method.
 *
 * @param exposure Shutter time in us.
 * @param iso ISO value.
 *
 * @return MMAL_SUCCESS if all OK, something else otherwise
 *
 */
int MMALCamera::exposure(long exposure, int iso)
{
    // Our main data storage vessel..
    char filename[] = "/dev/shm/indi_raspistill_capture.jpg";
    int exit_code = 0;

    MMAL_STATUS_T status = MMAL_SUCCESS;
    MMAL_PORT_T *camera_still_port = nullptr;

    bcm_host_init();

    // Register our application with the logging system
    vcos_log_register("RaspiStill", VCOS_LOG_CATEGORY);
    fprintf(stderr, "vcos_log_register done\n");

#if 0
// FIXME:    default_status(&state);

    state.preview_parameters.wantPreview = 0;
    state.timeout = 1;
    state.common_settings.verbose = 0;
    state.camera_parameters.shutter_speed = exposure * 1000000L;
    state.frameNextMethod = FRAME_NEXT_IMMEDIATELY;

    // Setup for sensor specific parameters
 // FIXME:   get_sensor_defaults(state.common_settings.cameraNum, state.common_settings.camera_name, &state.common_settings.width, &state.common_settings.height);
#endif

    create_camera_component();

    VCOS_STATUS_T vcos_status;

    // Set up our userdata - this is passed though to the callback where we need the information.
    // Null until we open our filename
    file_handle = nullptr;
    vcos_status = vcos_semaphore_create(&complete_semaphore, "RaspiStill-sem", 0);
    MMALException::throw_if(vcos_status != VCOS_SUCCESS, "Failed to create semaphore");

    /* Enable component */
    status = mmal_component_enable(camera);
    MMALException::throw_if(status != MMAL_SUCCESS, "camera component couldn't be enabled");

    // Open the file
    file_handle = fopen(filename, "wb");
    MMALException::throw_if(file_handle == nullptr, "Failed to open the capture file");

    unsigned int num, q;

    status = mmal_port_parameter_set_boolean( camera->output[MMAL_CAMERA_CAPTURE_PORT], MMAL_PARAMETER_ENABLE_RAW_CAPTURE, 1);
    MMALException::throw_if(status != MMAL_SUCCESS, "RAW was requested, but failed to enable");

    // There is a possibility that shutter needs to be set each loop.
    status = mmal_port_parameter_set_uint32(camera->control, MMAL_PARAMETER_SHUTTER_SPEED, 999 /* FIXME: Shutterspeed */);
    MMALException::throw_if(status != MMAL_SUCCESS, "Unable to set shutter speed");

    // Enable the camera output port and tell it its callback function
    status = mmal_port_enable(camera->output[MMAL_CAMERA_CAPTURE_PORT], c_callback);
    MMALException::throw_if(status != MMAL_SUCCESS, "Failed to enable camera port");

    // Send all the buffers to the encoder output port

    // FIXME: encoder_pool is 0 here.
    num = mmal_queue_length(pool->queue);
    for (q=0; q<num; q++)
    {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(pool->queue);

        if (!buffer)
            vcos_log_error("Unable to get a required buffer %d from pool queue", q);

        if (mmal_port_send_buffer(camera->output[MMAL_CAMERA_CAPTURE_PORT], buffer)!= MMAL_SUCCESS)
            vcos_log_error("Unable to send a buffer to encoder output port (%d)", q);
    }

    status = mmal_port_parameter_set_boolean(camera->output[MMAL_CAMERA_CAPTURE_PORT], MMAL_PARAMETER_CAPTURE, 1);
    MMALException::throw_if(status != MMAL_SUCCESS, "Failed to start capture");

    {

        fprintf(stderr, "mmal_port_parameter_set_boolean done\n");
        // Wait for capture to complete
        // For some reason using vcos_semaphore_wait_timeout sometimes returns immediately with bad parameter error
        // even though it appears to be all correct, so reverting to untimed one until figure out why its erratic
        vcos_semaphore_wait(&complete_semaphore);
    }

    // Ensure we don't die if get callback with no open file
    fclose(file_handle);
    file_handle = nullptr;

    vcos_semaphore_delete(&complete_semaphore);

    if (camera) {
        mmal_component_disable(camera);
    }

    return exit_code;
}

