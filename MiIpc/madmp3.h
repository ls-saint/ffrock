#ifndef _LIB_MAD_TEST_
#define _LIB_MAD_TEST_


typedef enum {
	PLAY_ACK_STATE = 0x00,
	PLAY_DOWNLOAD_STATE = 0x01,
	PLAY_ERROR_STATE = 0x02,
	PLAY_PLAYING_STATE = 0x03,
	PLAY_STOPED_STATE = 0x04,
	PLAY_PAUSE_STATE = 0x05,
	PLAY_QUERY_TIME = 0x06,
	PLAY_NETSLOWLY = 0x07,
}E_PLAY_STATUS;



int testMad(const char* filename);
int setPlayerStatus(E_PLAY_STATUS nPlayStatus);
int getPlayerStatus();
int getPlayerTime();
int getHypnosisTime();


#endif//END _LIB_MAD_TEST_