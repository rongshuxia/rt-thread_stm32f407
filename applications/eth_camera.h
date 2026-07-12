#ifndef __OV2640_JPEG_H__
#define __OV2640_JPEG_H__


#define JPEG_WIDTH_320			  	320
#define JPEG_HEIGHT_240			 	240
#define JPEG_BUF_SIZE  				(32 * 1024)


#define ETH_PORT					8001

uint32_t get_jpeg_frame(uint32_t *p_jpeg_buf);

void eth_camera_capture(void);

int eth_camera_is_streaming(void);

#endif
