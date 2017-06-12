/*
Driver API for Helios Laser DAC class, SOURCE
By Gitle Mikkelsen
gitlem@gmail.com

Dependencies:
Libusb 1.0 (GNU Lesser General Public License, see libusb.h)

Standard: C++14
git repo: https://github.com/Grix/helios_dac.git

See header HeliosDac.h for function and usage documentation
*/


#include "HeliosDac.h"

HeliosDac::HeliosDac()
{
	inited = false;
}

HeliosDac::~HeliosDac()
{
	CloseDevices();
}

int HeliosDac::OpenDevices()
{
	if (inited)
		return deviceList.size();

	int result = libusb_init(NULL);
	if (result < 0)
		return result;

	libusb_set_debug(NULL, LIBUSB_LOG_LEVEL);

	libusb_device** devs;
	ssize_t cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0)
		return (int)cnt;

	std::lock_guard<std::mutex> lock(threadLock);

	unsigned int devNum = 0;
	for (int i = 0; i < cnt; i++)
	{
		struct libusb_device_descriptor devDesc;
		int result = libusb_get_device_descriptor(devs[i], &devDesc);
		if (result < 0)
			continue;

		if ((devDesc.idProduct != HELIOS_PID) || (devDesc.idVendor != HELIOS_VID))
			continue;

		libusb_device_handle* devHandle;
		result = libusb_open(devs[i], &devHandle);
		if (result < 0)
			continue;

		result = libusb_claim_interface(devHandle, 0);
		if (result < 0)
			continue;

		result = libusb_set_interface_alt_setting(devHandle, 0, 1);
		if (result < 0)
			continue;

		//successfully opened, add to device list
		deviceList.push_back(std::make_unique<HeliosDacDevice>(devHandle));

		devNum++;
	}

	libusb_free_device_list(devs, 1);

	if (devNum > 0)
		inited = true;

	return devNum;
}

int HeliosDac::CloseDevices()
{
	if (!inited)
		return HELIOS_ERROR;
	
	std::lock_guard<std::mutex> lock(threadLock);
	inited = false;
	deviceList.clear(); //various destructors will clean all devices

	libusb_exit(NULL);

	return HELIOS_SUCCESS;
}

int HeliosDac::WriteFrame(unsigned int devNum, unsigned int pps, std::uint8_t flags, HeliosPoint* points, unsigned int numOfPoints)
{
	if (!inited)
		return HELIOS_ERROR;

	if ((points == NULL) || (numOfPoints > HELIOS_MAX_POINTS) || (pps > HELIOS_MAX_RATE) || (pps < HELIOS_MIN_RATE))
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL; 
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();

	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->SendFrame(pps, flags, points, numOfPoints);
}

int HeliosDac::GetStatus(unsigned int devNum)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->GetStatus();
}

int HeliosDac::GetFirmwareVersion(unsigned int devNum)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->GetFirmwareVersion();
}

int HeliosDac::GetName(unsigned int devNum, char* name)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	char* dacName = dev->GetName();
	if (dacName == "")
	{
		memcpy(name, "Helios ", 8);
		name[7] = (char)((int)(devNum >= 10) + 48);
		name[8] = (char)((int)(devNum % 10) + 48);
		name[9] = '\0';
	}
	else
	{
		memcpy(name, dacName, 32);
	}
	return HELIOS_SUCCESS;
}

int HeliosDac::SetName(unsigned int devNum, char* name)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->SetName(name);
}

int HeliosDac::Stop(unsigned int devNum)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->Stop();
}

int HeliosDac::SetShutter(unsigned int devNum, bool level)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->SetShutter(level);
}

int HeliosDac::EraseFirmware(unsigned int devNum)
{
	if (!inited)
		return HELIOS_ERROR;

	std::unique_lock<std::mutex> lock(threadLock);
	HeliosDacDevice* dev = NULL;
	if (devNum < deviceList.size())
		dev = deviceList[devNum].get();
	lock.unlock();
	if (dev == NULL)
		return HELIOS_ERROR;

	return dev->EraseFirmware();
}


/// -----------------------------------------------------------------------
/// HELIOSDACDEVICE START (one instance for each connected DAC)
/// -----------------------------------------------------------------------

HeliosDac::HeliosDacDevice::HeliosDacDevice(libusb_device_handle* handle)
{
	closed = true;
	usbHandle = handle;
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	std::lock_guard<std::mutex>lock(frameLock);

	int actualLength = 0;
	bool ok = false;

	//catch any lingering transfers
	std::uint8_t ctrlBuffer0[32];
	while (libusb_interrupt_transfer(usbHandle, EP_INT_IN, ctrlBuffer0, 32, &actualLength, 5) == LIBUSB_SUCCESS);

	//get firmware version
	firmwareVersion = 0;
	bool repeat = true;
	for (int i = 0; ((i < 2) && repeat); i++) //retry command if necessary
	{
		std::uint8_t ctrlBuffer[2] = { 0x04, 0 };
		int transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_OUT, ctrlBuffer, 2, &actualLength, 32);
		if ((transferResult == LIBUSB_SUCCESS) && (actualLength == 2))
		{
			for (int j = 0; ((j < 3) && repeat); j++) //retry response getting if necessary
			{
				std::uint8_t ctrlBuffer2[32];
				transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_IN, ctrlBuffer2, 32, &actualLength, 32);
				if (transferResult == LIBUSB_SUCCESS)
				{
					if (ctrlBuffer2[0] == 0x84)
					{
						firmwareVersion = ((ctrlBuffer2[1] << 0) |
							(ctrlBuffer2[2] << 8) |
							(ctrlBuffer2[3] << 16) |
							(ctrlBuffer2[4] << 24));
						repeat = false;
					}
				}
			}
		}
	}


	//send sdk firmware version
	repeat = true;
	for (int i = 0; ((i < 2) && repeat); i++) //retry command if necessary
	{
		std::uint8_t ctrlBuffer3[2] = { 0x07, HELIOS_SDK_VERSION };
		int transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_OUT, ctrlBuffer3, 2, &actualLength, 32);
		if ((transferResult == LIBUSB_SUCCESS) && (actualLength == 2))
			repeat = false;
	}

	frameBuffer = new std::uint8_t[HELIOS_MAX_POINTS * 7 + 5];

	closed = false;

	std::thread frameHandlerThread(&HeliosDac::HeliosDacDevice::FrameHandler, this);
	frameHandlerThread.detach();
}

//sends a raw frame buffer (implemented as bulk transfer) to a dac device
//returns 1 if success
int HeliosDac::HeliosDacDevice::SendFrame(unsigned int pps, std::uint8_t flags, HeliosPoint* points, unsigned int numOfPoints)
{
	if (closed || frameReady)
		return HELIOS_ERROR;

	unsigned int bufPos = 0;
	
	//this is a bug workaround, the mcu won't correctly receive transfers with these sizes
	unsigned int ppsActual = pps;
	unsigned int numOfPointsActual = numOfPoints;
	if ((((int)numOfPoints-45) % 64) == 0)
	{
		numOfPointsActual--;
		//adjust pps to keep the same frame duration even with one less point
		ppsActual = (unsigned int)((pps * (double)numOfPointsActual / (double)numOfPoints) + 0.5); 
	}

	//prepare frame buffer
	for (unsigned int i = 0; i < numOfPointsActual; i++)
	{
		frameBuffer[bufPos++] = (points[i].x >> 4);
		frameBuffer[bufPos++] = ((points[i].x & 0x0F) << 4) | (points[i].y >> 8);
		frameBuffer[bufPos++] = (points[i].y & 0xFF);
		frameBuffer[bufPos++] = points[i].r;
		frameBuffer[bufPos++] = points[i].g;
		frameBuffer[bufPos++] = points[i].b;
		frameBuffer[bufPos++] = points[i].i;
	}
	frameBuffer[bufPos++] = (ppsActual & 0xFF);
	frameBuffer[bufPos++] = (ppsActual >> 8);
	frameBuffer[bufPos++] = (numOfPointsActual & 0xFF);
	frameBuffer[bufPos++] = (numOfPointsActual >> 8);
	frameBuffer[bufPos++] = flags;

	frameBufferSize = bufPos;

	if ((flags & HELIOS_FLAGS_DONT_BLOCK) != 0)
	{
		frameReady = true;
		return HELIOS_SUCCESS;
	}
	else
	{
		return DoFrame();
	}
}

//continually running thread, when a frame is ready, it is sent to the DAC
void HeliosDac::HeliosDacDevice::FrameHandler()
{ 
	while (!closed)
	{
		//wait until frame is ready to be sent
		while ((!frameReady) && (!closed))
			std::this_thread::sleep_for(std::chrono::microseconds(100));

		if (closed)
			return;

		DoFrame();

		frameReady = false;
	}
}

//sends frame to DAC
int HeliosDac::HeliosDacDevice::DoFrame()
{
	if (closed)
		return HELIOS_ERROR;

	int actualLength = 0;
	int transferResult = libusb_bulk_transfer(usbHandle, EP_BULK_OUT, frameBuffer, frameBufferSize, &actualLength, 8 + (frameBufferSize >> 5));

	if (transferResult == LIBUSB_SUCCESS)
		return HELIOS_SUCCESS;
	else
		return HELIOS_ERROR;
}

//Gets firmware version of DAC
int HeliosDac::HeliosDacDevice::GetFirmwareVersion()
{
	if (closed)
		return HELIOS_ERROR;

	return firmwareVersion;
}

//Gets firmware version of DAC
char* HeliosDac::HeliosDacDevice::GetName()
{
	if (closed)
		return "";

	std::lock_guard<std::mutex> lock(frameLock);

	bool gotName = false;
	
	bool repeat = true;
	for (int i = 0; ((i < 2) && repeat); i++) //retry command if necessary
	{
		int actualLength = 0;
		std::uint8_t ctrlBuffer4[2] = { 0x05, 0 };
		if (SendControl(ctrlBuffer4, 2) == HELIOS_SUCCESS)
		{
			std::uint8_t ctrlBuffer5[32];
			int transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_IN, ctrlBuffer5, 32, &actualLength, 32);

			if (transferResult == LIBUSB_SUCCESS)
			{
				if (ctrlBuffer5[0] == 0x85)
				{
					ctrlBuffer5[31] = '\0';
					memcpy(name, &ctrlBuffer5[1], 31);
					gotName = true;
					repeat = false;
				}
			}
		}
	}

	//if the above failed, fallback name:
	if (!gotName)
	{
		return "";
	}

	return name;
}

//Gets status of DAC, 1 means DAC is ready to receive frame, 0 means it's not
int HeliosDac::HeliosDacDevice::GetStatus()
{
	if (closed)
		return HELIOS_ERROR;

	std::lock_guard<std::mutex> lock(frameLock);

	int actualLength = 0;
	std::uint8_t ctrlBuffer[2] = { 0x03, 0 };
	if (SendControl(ctrlBuffer, 2) == HELIOS_SUCCESS)
	{
		std::uint8_t ctrlBuffer2[32];
		int transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_IN, ctrlBuffer2, 32, &actualLength, 16);
		if (transferResult == LIBUSB_SUCCESS)
		{
			if (ctrlBuffer2[0] == 0x83) //STATUS ID
			{
				if (ctrlBuffer2[1] == 0)
					return 0;
				else
					return 1;
			}
		}
	}

	return HELIOS_ERROR;
}

//Set shutter level of DAc
int HeliosDac::HeliosDacDevice::SetShutter(bool level)
{
	if (closed)
		return HELIOS_ERROR;

	std::lock_guard<std::mutex> lock(frameLock);

	std::uint8_t txBuffer[2] = { 0x02, level };
	if (SendControl(txBuffer, 2) == HELIOS_SUCCESS)
		return HELIOS_SUCCESS;
	else
		return HELIOS_ERROR;
}

//Stops output of DAC
int HeliosDac::HeliosDacDevice::Stop()
{
	if (closed)
		return HELIOS_ERROR;

	std::lock_guard<std::mutex> lock(frameLock);

	std::uint8_t txBuffer[2] = { 0x01, 0 };
	if (SendControl(txBuffer, 2) == HELIOS_SUCCESS)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		return HELIOS_SUCCESS;
	}
	else
		return HELIOS_ERROR;
}

//Sets persistent name of DAC
int HeliosDac::HeliosDacDevice::SetName(char* name)
{
	if (closed)
		return HELIOS_ERROR;

	std::lock_guard<std::mutex> lock(frameLock);

	std::uint8_t txBuffer[32] = { 0x06 };
	memcpy(&txBuffer[1], name, 30);
	txBuffer[31] = '\0';
	if (SendControl(txBuffer, 32) == HELIOS_SUCCESS)
		return HELIOS_SUCCESS;
	else
		return HELIOS_ERROR;
}

//Erases the firmware of the DAC, allowing it to be updated
int HeliosDac::HeliosDacDevice::EraseFirmware()
{
	if (closed)
		return HELIOS_ERROR;

	std::lock_guard<std::mutex> lock(frameLock);

	std::uint8_t txBuffer[2] = { 0xDE, 0 };
	if (SendControl(txBuffer, 2) == HELIOS_SUCCESS)
	{
		closed = true;
		return HELIOS_SUCCESS;
	}
	else
		return HELIOS_ERROR;
}

//sends a raw control signal (implemented as interrupt transfer) to a dac device
//returns 1 if successful
int HeliosDac::HeliosDacDevice::SendControl(std::uint8_t* bufferAddress, unsigned int length)
{
	if ((bufferAddress == NULL) || (length > 32))
		return HELIOS_ERROR;

	int actualLength = 0;
	int transferResult = libusb_interrupt_transfer(usbHandle, EP_INT_OUT, bufferAddress, length, &actualLength, 16);

	if (transferResult == LIBUSB_SUCCESS)
		return HELIOS_SUCCESS;
	else
		return HELIOS_ERROR;
}

HeliosDac::HeliosDacDevice::~HeliosDacDevice()
{
	closed = true;
	std::lock_guard<std::mutex>lock(frameLock); //wait until all threads have closed

	libusb_close(usbHandle);
	delete frameBuffer;
}
