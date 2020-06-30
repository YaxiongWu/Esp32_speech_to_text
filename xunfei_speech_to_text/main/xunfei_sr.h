/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _BAIDU_SR_H_
#define _BAIDU_SR_H_

#include "esp_err.h"
#include "audio_event_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_SR_BUFFER_SIZE (2048)

//#define DEFAULT_PCM_FILE_BUFFER_SIZE (100000)

/**
 * Google Cloud Speech-to-Text audio encoding
 */
typedef enum {
    ENCODING_LINEAR16 = 0,  /*!< Google Cloud Speech-to-Text audio encoding PCM 16-bit mono */
} baidu_sr_encoding_t;

typedef struct baidu_sr* baidu_sr_handle_t;
typedef void (*baidu_sr_event_handle_t)(baidu_sr_handle_t sr);

/**
 * Google Cloud Speech-to-Text configurations
 * 

{
    "format":"pcm",
    "rate":16000,
    "dev_pid":1536,
    "channel":1,
    "token":xxx,
    "cuid":"baidu_workshop",
    "len":4096,
    "speech":"xxx", // xxx为 base64（FILE_CONTENT）
}
*/

typedef struct {
   const char *format;            
   const char *token;
   const char *cuid;
   const char *api_key;                /*!< API Key */
   const char *lang_code;              /*!< Speech-to-Text language code */
   int record_sample_rates;            /*!< Audio recording sample rate */
   baidu_sr_encoding_t encoding;      /*!< Audio encoding */
   int buffer_size;                    /*!< Processing buffer size */
   baidu_sr_event_handle_t on_begin;  /*!< Begin send audio data to server */
} baidu_sr_config_t;

/*
typedef struct {
   const char *format;                
   int rate;
   int dev_pid;
   int channel;
   const char *token;
   const char *cuid;
   int len;
   const char *speech;              
   baidu_sr_event_handle_t on_begin; 
} baidu_sr_config_t;
 */



/**
 * @brief      initialize Google Cloud Speech-to-Text, this function will return a Speech-to-Text context
 *
 * @param      config  The Google Cloud Speech-to-Text configuration
 *
 * @return     The Speech-to-Text context
 */
baidu_sr_handle_t baidu_sr_init(baidu_sr_config_t *config);

/**
 * @brief      Start recording and sending audio to Google Cloud Speech-to-Text
 *
 * @param[in]  sr   The Speech-to-Text context
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t baidu_sr_start(baidu_sr_handle_t sr);

/**
 * @brief      Stop sending audio to Google Cloud Speech-to-Text and get the result text
 *
 * @param[in]  sr   The Speech-to-Text context
 *
 * @return     Google Cloud Speech-to-Text server response
 */
char *baidu_sr_stop(baidu_sr_handle_t sr);

/**
 * @brief      Cleanup the Speech-to-Text object
 *
 * @param[in]  sr   The Speech-to-Text context
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_sr_destroy(baidu_sr_handle_t sr);

/**
 * @brief      Register listener for the Speech-to-Text context
 *
 * @param[in]   sr   The Speech-to-Text context
 * @param[in]  listener  The listener
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t baidu_sr_set_listener(baidu_sr_handle_t sr, audio_event_iface_handle_t listener);
//static int get_access_token(void);

#ifdef __cplusplus
}
#endif

#endif
