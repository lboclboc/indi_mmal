/**
 * INDI driver for Raspberry Pi 12Mp High Quality camera.
 */
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mmaldriver.h"

#define DEFAULT_ISO 400

extern "C" {
    extern int raspi_exposure(long exposure, int iso_speed);
}

MMALDriver::MMALDriver()
{
    setVersion(1, 0);
}

MMALDriver::~MMALDriver()
{
}

void MMALDriver::assert_framebuffer(INDI::CCDChip *ccd)
{
    int nbuf = (ccd->getXRes() * ccd->getYRes() * (ccd->getBPP() / 8));
    int expected = 4056 * 3040 * 2;
    if (nbuf != expected) {
        LOGF_DEBUG("%s: frame buffer size set to %d", __FUNCTION__, nbuf);
        LOGF_ERROR("%s: Wrong size of framebuffer: %d, expected %d", __FUNCTION__, nbuf, expected);
        exit(1);
    }

    LOGF_DEBUG("%s: frame buffer size set to %d", __FUNCTION__, nbuf);
}

bool MMALDriver::saveConfigItems(FILE * fp)
{
    // ISO Settings
    if (mIsoSP.nsp > 0) {
        IUSaveConfigSwitch(fp, &mIsoSP);
    }

    return true;
}

void MMALDriver::addFITSKeywords(fitsfile * fptr, INDI::CCDChip * targetChip)
{
    INDI::CCD::addFITSKeywords(fptr, targetChip);

    int status = 0;

    if (mIsoSP.nsp > 0)
    {
        ISwitch * onISO = IUFindOnSwitch(&mIsoSP);
        if (onISO)
        {
            int isoSpeed = atoi(onISO->label);
            if (isoSpeed > 0) {
                fits_update_key_s(fptr, TUINT, "ISOSPEED", &isoSpeed, "ISO Speed", &status);
            }
        }
    }
}

/**************************************************************************************
 * Client is asking us to establish connection to the device
 **************************************************************************************/
bool MMALDriver::Connect()
{
    DEBUG(INDI::Logger::DBG_SESSION, "MMAL device connected successfully!");

    SetTimer(POLLMS);
    return true;
}

/**************************************************************************************
 * Client is asking us to terminate connection to the device
 **************************************************************************************/
bool MMALDriver::Disconnect()
{
    DEBUG(INDI::Logger::DBG_SESSION, "MMAL device disconnected successfully!");
    return true;
}

/**************************************************************************************
 * INDI is asking us for our default device name
 **************************************************************************************/
const char * MMALDriver::getDefaultName()
{
    return "MMAL Device";
}

void MMALDriver::ISGetProperties(const char * dev)
{
    if (dev != nullptr && strcmp(getDeviceName(), dev) != 0)
        return;

    INDI::CCD::ISGetProperties(dev);
}

bool MMALDriver::initProperties()
{
    // We must ALWAYS init the properties of the parent class first
    INDI::CCD::initProperties();

    LOGF_DEBUG("%s: updateProperties()", __FUNCTION__);

    addDebugControl();

    SetCCDCapability(0
		| CCD_CAN_BIN			// Does the CCD support binning?
		| CCD_CAN_SUBFRAME		// Does the CCD support setting ROI?
	//	| CCD_CAN_ABORT			// Can the CCD exposure be aborted?
	//	| CCD_HAS_GUIDE_HEAD	// Does the CCD have a guide head?
	//	| CCD_HAS_ST4_PORT 		// Does the CCD have an ST4 port?
	//	| CCD_HAS_SHUTTER 		// Does the CCD have a mechanical shutter?
	//	| CCD_HAS_COOLER 		// Does the CCD have a cooler and temperature control?
		| CCD_HAS_BAYER 		// Does the CCD send color data in bayer format?
	//	| CCD_HAS_STREAMING 	// Does the CCD support live video streaming?
	//	| CCD_HAS_WEB_SOCKET 	// Does the CCD support web socket transfers?
	);

    setDefaultPollingPeriod(500);

    // ISO switches
    IUFillSwitch(&mIsoS[0], "ISO_100", "100", ISS_OFF);
    IUFillSwitch(&mIsoS[1], "ISO_200", "200", ISS_OFF);
    IUFillSwitch(&mIsoS[2], "ISO_400", "400", ISS_ON);
    IUFillSwitch(&mIsoS[3], "ISO_800", "800", ISS_OFF);
    IUFillSwitchVector(&mIsoSP, mIsoS, 4, getDeviceName(), "CCD_ISO", "ISO", IMAGE_SETTINGS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 0.001, 1000, .0001, false);
//    PrimaryCCD.setCompressed(false);
//    PrimaryCCD.setImageExtension("raw"); // FIXME: use FITS instead

    SetCCDParams(4056, 3040, 16, 1.55L, 1.55L);

    UpdateCCDFrame(0, 0, 4056, 3040);

    return true;
}

bool MMALDriver::updateProperties()
{
	// We must ALWAYS call the parent class updateProperties() first
    INDI::CCD::updateProperties();

    LOGF_DEBUG("%s: updateProperties()", __FUNCTION__);

    if (isConnected())  {
        if (mIsoSP.nsp > 0) {
            defineSwitch(&mIsoSP);
        }
    }
    else {
        if (mIsoSP.nsp > 0) {
            deleteProperty(mIsoSP.name);
        }
    }

	return true;
}

// FIXME: doc
bool MMALDriver::UpdateCCDBin(int hor, int ver)
{
	// FIXME: implement UpdateCCDBin
    LOGF_DEBUG("%s: UpdateCCDBin(%d, %d)", __FUNCTION__, hor, ver);

    return true;
}

/**************************************************************************************
 * CCD calls this function when CCD Frame dimension needs to be updated in the hardware.
 * Derived classes should implement this function.
 **************************************************************************************/
bool MMALDriver::UpdateCCDFrame(int x, int y, int w, int h)
{
	LOGF_DEBUG("UpdateCCDFrame(%d, %d, %d, %d", x, y, w, h);

    // FIXME: handle cropping
    if (x + y != 0) {
        LOGF_ERROR("%s: origin offset not supported.", __FUNCTION__);
    }

    // Let's calculate how much memory we need for the primary CCD buffer
    int nbuf = (PrimaryCCD.getXRes() * PrimaryCCD.getYRes() * (PrimaryCCD.getBPP() / 8));

    LOGF_DEBUG("%s: frame buffer size set to %d", __FUNCTION__, nbuf);

    PrimaryCCD.setFrameBufferSize(nbuf);

    return true;
}

/**************************************************************************************
 * Client is asking us to start an exposure
 * \param duration exposure time in seconds.
 **************************************************************************************/
bool MMALDriver::StartExposure(float duration)
{

    if (InExposure)
    {
        LOG_ERROR("Camera is already exposing.");
        return false;
    }

    LOGF_DEBUG("StartEposure(%f)", duration);

    ExposureRequest = duration;

    // Since we have only have one CCD with one chip, we set the exposure duration of the primary CCD
    PrimaryCCD.setExposureDuration(duration);

    gettimeofday(&ExpStart, nullptr);

    InExposure = true;

    // Return true for this will take some time.
    return true;
}


/**************************************************************************************
 * Client is asking us to abort an exposure
 **************************************************************************************/
bool MMALDriver::AbortExposure()
{
	LOGF_DEBUG("AbortEposure()", 0);
    InExposure = false;
    // FIXME: Needs to be handled.
    return true;
}

/**************************************************************************************
 * How much longer until exposure is done?
 **************************************************************************************/
float MMALDriver::CalcTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now { 0, 0 };
    gettimeofday(&now, nullptr);

    timesince = (double)(now.tv_sec * 1000.0 + now.tv_usec / 1000) -
                (double)(ExpStart.tv_sec * 1000.0 + ExpStart.tv_usec / 1000);
    timesince = timesince / 1000;

    timeleft = ExposureRequest - timesince;

    if (timeleft < 0) {
        timeleft = 0;
    }

    return timeleft;
}

/**************************************************************************************
 * Main device loop. We check for exposure
 **************************************************************************************/
void MMALDriver::TimerHit()
{
    uint32_t nextTimer = POLLMS;

    if (!isConnected()) {
        return; //  No need to reset timer if we are not connected anymore
    }

    if (InExposure)
    {
        float timeleft = CalcTimeLeft();
        if (timeleft < 0)
            timeleft = 0;

        // FIXME: make capturing occur in separate thread.
        timeleft = 0;

        // Just update time left in client
        PrimaryCCD.setExposureLeft(timeleft);

        // Less than a 1 second away from exposure completion, use shorter timer. If less than 1m, take the image.
        if (timeleft < 1.0) {
            if (timeleft < 0.001) {
				/* We're done exposing */
				IDMessage(getDeviceName(), "Exposure done, downloading image...");

				// Set exposure left to zero
				PrimaryCCD.setExposureLeft(0);

				// We're no longer exposing...
				InExposure = false;

				/* grab and save image */
				grabImage();
            }
            else {
                nextTimer = timeleft * 1000;
            }
        }
    }

    SetTimer(nextTimer);
}

/**************************************************************************************
 * Create a random image and return it to client
 **************************************************************************************/
void MMALDriver::grabImage()
{
    // Let's get a pointer to the frame buffer
    uint8_t *image = PrimaryCCD.getFrameBuffer();
    const char filename[] = "/dev/shm/indi_raspistill_capture.jpg";

    // Perform the actual exposure.
    // FIXME: Should be a separate thread, this thread should just be waiting.
    int isoSpeed = DEFAULT_ISO;
    ISwitch * onISO = IUFindOnSwitch(&mIsoSP);
    if (onISO) {
        isoSpeed = atoi(onISO->label);
    }

    raspi_exposure(ExposureRequest, isoSpeed);
    fprintf(stderr,"Image exposed to %s.\n", filename);

    FILE *fp = fopen(filename, "rb");
    if (!fp)  {
        LOGF_ERROR("%s: Failed to open %s: %s", __FUNCTION__, filename, strerror(errno));
        exit(1);
    }

    struct stat statbuf;
    if (stat(filename, &statbuf) != 0) {
        LOGF_ERROR("%s: Failed to stat %s file: %s", __FUNCTION__, filename, strerror(errno));
        exit(1);
    }

    // FIXME: remove all hardcoding for IMAX477 camera. Should at least try to parse the BCRM header.
    const int raw_file_size = 18711040;
    const int brcm_header_size = 32768;
    int bytes_to_fill = PrimaryCCD.getFrameBufferSize();

    assert_framebuffer(&PrimaryCCD);

    if (fseek(fp, statbuf.st_size - raw_file_size, SEEK_SET) != 0)  {
        LOGF_ERROR("%s: Wrong size of %s: %s", __FUNCTION__, filename, strerror(errno));
        exit(1);
    }

    {
        char brcm_header[brcm_header_size];
        fread(brcm_header, brcm_header_size, 1, fp);
        if (strcmp(brcm_header, "BRCMo")) {
            LOGF_ERROR("%s: Missing BRCMo header, found %.10s as pos %ld", __FUNCTION__, brcm_header, ftell(fp));
            exit(1);
        }
    }
    std::unique_lock<std::mutex> guard(ccdBufferLock);
    {
        const int raw_row_size = 6112;
        const int trailer = 28;
        uint8_t row[raw_row_size];
        int i = 0;
        while(i < bytes_to_fill)
        {
            fread(row, raw_row_size, 1, fp);
            if (ferror(fp)) {
                    LOGF_ERROR("%s: Failed to read from file: %s, retrying..", __FUNCTION__, strerror(errno));
                    sleep(1);
            }

            for(int p = 0; p < raw_row_size - trailer; p += 3)
            {
                uint16_t v1 = static_cast<uint16_t>(row[p] + ((row[p+2]&0xF)<<8));
                uint16_t v2 = static_cast<uint16_t>(row[p+1] + ((row[p+2]&0xF0)<<4));
                image[i++] = (v1 >> 8) & 0xFF;
                image[i++] = v1 & 0xFF;
                image[i++] = (v2 >> 8) & 0xFF;
                image[i++] = v2 & 0xFF;
            }
        }
    }
    fclose(fp);
    unlink(filename);

    guard.unlock();

    IDMessage(getDeviceName(), "Download complete.");

    // Let INDI::CCD know we're done filling the image buffer
    ExposureComplete(&PrimaryCCD);
}

bool MMALDriver::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    LOGF_DEBUG("%s: dev=%s, name=%s", __FUNCTION__, dev, name);

    // ignore if not ours
    if (dev != nullptr && strcmp(dev, getDeviceName()) != 0)
        return false;

    if (INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n))
        return true;

    ISwitchVectorProperty *svp = getSwitch(name);
    if (!isConnected()) {
         svp->s = IPS_ALERT;
         IDSetSwitch(svp, "Cannot change property while device is disconnected.");
         return false;
    }

    // FIXME: When implementing variables here, make sure to call void MMALDriver::updateFrameBufferSize()
    if (!strcmp(name, mIsoSP.name))
    {
        if (IUUpdateSwitch(&mIsoSP, states, names, n) < 0) {
            return false;
        }

        IDSetSwitch(&mIsoSP, nullptr);
        return true;
    }

    return false;
}

bool MMALDriver::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    LOGF_DEBUG("%s: dev=%s, name=%s", __FUNCTION__, dev, name);

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

bool MMALDriver::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    LOGF_DEBUG("%s: dev=%s, name=%s", __FUNCTION__, dev, name);

    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}
