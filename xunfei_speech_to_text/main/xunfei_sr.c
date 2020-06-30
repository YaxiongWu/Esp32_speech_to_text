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

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mbedtls/base64.h"

#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "json_utils.h"

#include "board.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "periph_led.h"
#include "xunfei_sr.h"
#include "baidu_access_token.h"

//#include "apps/sntp/sntp.h"
#include "esp_sntp.h"
#include "crypto/includes.h"
#include "crypto/common.h"
#include "crypto/sha256.h"
#include "crypto/crypto.h"

/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include <stdio.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"


#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "audio_url.h"
#include "cJSON.h"

static const char *TAG = "BAIDU_SR";
static char *baidu_access_token = NULL;

#define BAIDU_SR_ENDPOINT  "http://vop.baidu.com/server_api"
//#define BAIDU_SR_ENDPOINT  "http://192.168.43.171:8000/upload"
//#define BAIDU_SR_CONFIG           "{\"languageCode\": \"%s\", \"encoding\": \"%s\", \"sampleRateHertz\": %d}"
//#define BAIDU_SR_BEGIN            "{\"config\": " BAIDU_SR_CONFIG ", \"audio\": {\"content\":\""
//#define BAIDU_SR_CONFIG           "dev_pid=1536&cuid=xxxxx&token=24.f73a28b84aa7285aa69079a610d9a9ed.2592000.1563181441.282335-16147548"
#define BAIDU_SR_BEGIN            '{"dev_pid":1537,"rate":16000,"channel":1,"cuid":"%s","format":"%s","token":"%s","speech":"'
#define BAIDU_SR_END              '","len":%d}'
#define BAIDU_SR_TASK_STACK (8*1024)
#define FIRST_PACKET_PRE_DATA '{"common": {"app_id": "%s"}, "business": {"domain": "iat", "language": "zh_cn", "accent": "mandarin", "vinfo": 1, "vad_eos": 10000}, "data": {"status": 0, "format": "audio/L16;rate=16000", "audio":"'
#define MIDDLE_PACKET_PRE_DATA '{"data": {"status": 1, "format": "audio/L16;rate=16000", "audio":"'
#define LAST_PACKET_PRE_DATA '{"data": {"status": 2, "format": "audio/L16;rate=16000", "audio":"'
#define PACKET_END_DATA  '", "encoding": "raw"}}'
#define EXAMPLE_RECORD_PLAYBACK_SAMPLE_RATE (16000)
#define AUTH_URL_LENGTH     330
esp_periph_handle_t led_handle = NULL;
typedef struct baidu_sr {
    audio_pipeline_handle_t pipeline;  
    int                     remain_len;
    int                     sr_total_write;
    bool                    is_begin;
    char                    *buffer;
    char                    *b64_buffer;
    audio_element_handle_t  i2s_reader;
    audio_element_handle_t  http_stream_writer;
    char                    *cuid;
    char                    *format;
    char                    *token;
    char                    *auth_Url;
    int                     sample_rates;
    int                     buffer_size;
    baidu_sr_encoding_t    encoding;
    char                    *response_text;
    baidu_sr_event_handle_t on_begin;
} baidu_sr_t;

esp_websocket_client_config_t websocket_cfg = {};
esp_websocket_client_handle_t client=NULL;

#define NO_DATA_TIMEOUT_SEC 10


static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}
void initialize_sntp(void)
{
    ESP_LOGI(TAG, "------------Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
//    sntp_setservername(0, "cn.pool.ntp.org");	//设置访问服务器	国外提供商
    sntp_setservername(0, "cn.ntp.org.cn");	//设置访问服务器	中国提供商 
    sntp_init();
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1); 
    tzset();
}

struct tm get_time_from_net(void)
{
    initialize_sntp();
	time_t now = 0;
    struct tm timeinfo = { 0 };
    //char strftime_buf[72];      
    //time(&now);	
    //localtime_r(&now, &timeinfo);	
    while(timeinfo.tm_year < (2018 - 1900)) {
     ESP_LOGE(TAG, "the date:%d-%d-%d %d:%d:%d,retry to get time from SNTP in 5S!",
    		timeinfo.tm_year+1900,
			timeinfo.tm_mon+1, timeinfo.tm_mday,
			timeinfo.tm_hour, timeinfo.tm_min,
			timeinfo.tm_sec);
     time(&now);  //获取网络时间， 64bit的秒计数
     localtime_r(&now, &timeinfo);  //转换成具体的时间参数
     vTaskDelay(5000 / portTICK_PERIOD_MS);     
  
     //time_t timeCurr = time(NULL);                      
      //char timeStr[128];
      //(Wed, 10 Jul 2019 07:35:43 GMT)
      //strftime(timeStr, sizeof(timeStr), "%a,%d %b %Y %H:%M:%S", localtime(&timeCurr));
      //ESP_LOGE(TAG, "The timeStr: %s", timeStr);
    }  
    ESP_LOGE(TAG, "date:%d-%d-%d %d:%d:%d",
    		timeinfo.tm_year+1900,
			timeinfo.tm_mon+1, timeinfo.tm_mday,
			timeinfo.tm_hour, timeinfo.tm_min,
			timeinfo.tm_sec);
    sntp_stop();
	return timeinfo;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

char *assembleAuthUrl(void)
{
    static const uint8_t api_secret[]=CONFIG_Xunfei_APISecret;
    unsigned char sha256_base64[64];
    uint8_t num=0;
    time_t time_curr = time(NULL);
    char time_str[32];
    char *url = calloc(1, AUTH_URL_LENGTH);
    //url[270] ="wss://ws-api.xfyun.cn/v2/iat?authorization=";
    
    char pre_data[82];
    unsigned char authorization[220];
    char authorization_origin[164];
    char time_str_url[35];
    unsigned char hamc_sha256_result[32];
    

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();

    // get_time_from_net();  
    //(Wed, 10 Jul 2019 07:35:43 GMT)
    strftime(time_str, sizeof(time_str), "%a,%d %b %Y %H:%M:%S GMT", localtime(&time_curr));
    ESP_LOGI(TAG, "The time: %s", time_str);
    //char time_str[]="Fri,19 Jun 2020 18:18:38 GMT";
    
     //unsigned char *data='host: ws-api.xfyun.cn\ndate: Wed,17 Jun 2020 16:04:01 GMT\nGET /v2/iat HTTP/1.1';
   
     
     snprintf(pre_data,82,"host: ws-api.xfyun.cn\ndate: %s%s",time_str,"\nGET /v2/iat HTTP/1.1");
     //hmac_sha256('3d147e039eb43b3cd07cdbc3003b7d72', 32, 'host: ws-api.xfyun.cn\ndate: Wed,17 Jun 2020 16:04:01 GMT\nGET /v2/iat HTTP/1.1',sizeof(data), hamc_sha256_result);
     hmac_sha256(api_secret, 32, (uint8_t *)pre_data,strlen(pre_data), hamc_sha256_result);
     //ESP_LOGE(TAG,"pre_data:%s,lenstr:%d",pre_data,strlen(pre_data));
    // ESP_LOGE(TAG,"hamc_sha256_result:%x %x %x %x %x %x",hamc_sha256_result[0],hamc_sha256_result[1],
    //              hamc_sha256_result[2],hamc_sha256_result[3],hamc_sha256_result[4],hamc_sha256_result[5]);
   
     
     mbedtls_base64_encode(sha256_base64,sizeof(sha256_base64),&num,hamc_sha256_result,sizeof(hamc_sha256_result));

    // ESP_LOGE(TAG,"chBase64:%s",sha256_base64);

     
     snprintf(authorization_origin,sizeof(authorization_origin),"api_key=\"%s\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"%s\"",
          CONFIG_Xunfei_APIKey,  sha256_base64);
     //ESP_LOGE(TAG,"authorization_origin:%s",authorization_origin);
    
     mbedtls_base64_encode(authorization,sizeof(authorization),&num,(unsigned char*)authorization_origin,strlen((char *)authorization_origin));
    // ESP_LOGE(TAG,"authorization:%s",authorization);

     //ESP_LOGE(TAG,"size:%d",sizeof(authorization)+strlen(url)+6);
     //snprintf(url,sizeof(authorization)+strlen(url)+6,"%s%s%s","wss://ws-api.xfyun.cn/v2/iat?authorization=",authorization,"&date=");
    // ESP_LOGE(TAG,"url1:%s",url);
     //char *url_encode_date=audio_url_encode(time_str);
     size_t len_str=strlen(time_str),i,count=0;
     for(i=0;i<len_str;i++)
     {
        if(time_str[i]==',')
        {
           time_str_url[count++]='%';
           time_str_url[count++]='2';
           time_str_url[count++]='C';
        }else  if(time_str[i]==' ')
        {
          time_str_url[count++]='+';
        }else if(time_str[i]==':')
        {
          time_str_url[count++]='%';
           time_str_url[count++]='3';
           time_str_url[count++]='A';
        }else
        {
            time_str_url[count]=time_str[i];
            count++;
        }
    }
    time_str_url[count]='\0';
     ESP_LOGE(TAG,"time_str_url:%s",time_str_url);
     //ESP_LOGE(TAG,"strlen(url):%d",strlen(url));
     snprintf(url,AUTH_URL_LENGTH,"%s%s%s%s%s","wss://ws-api.xfyun.cn/v2/iat?authorization=",
                                   authorization,"&date=",time_str_url,"&host=ws-api.xfyun.cn");//+strlen(url_encode_date)
    // ESP_LOGE(TAG,"url2:%s",url);
     return url;
   /*  
    websocket_cfg.uri = url;

    ESP_LOGI(TAG, "websocket_cfg.uri:%s...", websocket_cfg.uri);


    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
    char data[32];
    i = 0;
    while (i < 10) {
        if (esp_websocket_client_is_connected(client)) {
            int len = sprintf(data, "hello %04d", i++);
            ESP_LOGI(TAG, "Sending %s", data);
            //esp_websocket_client_send(client, data, len, portMAX_DELAY);
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    esp_websocket_client_stop(client);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);
    */
    
}





void baidu_sr_begin(baidu_sr_handle_t sr)
{
    if (led_handle) {
        periph_led_blink(led_handle, get_green_led_gpio(), 500, 500, true, -1);
    }
    ESP_LOGW(TAG, "Start speaking now");
   
}
    
static int _http_write_chunk(esp_http_client_handle_t http, const char *buffer, int len)
{
    char header_chunk_buffer[16];
    int header_chunk_len = sprintf(header_chunk_buffer, "%x\r\n", len);
    if (esp_http_client_write(http, header_chunk_buffer, header_chunk_len) <= 0) {
        return ESP_FAIL;
    }
    int write_len = esp_http_client_write(http, buffer, len);
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write chunked content");
        return ESP_FAIL;
    }
    if (esp_http_client_write(http, "\r\n", 2) <= 0) {
        return ESP_FAIL;
    }
    return write_len;
}

static esp_err_t _http_stream_writer_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    baidu_sr_t *sr = (baidu_sr_t *)msg->user_data;
    int write_len;
    size_t need_write = 0,pre_data_len=0,middle_data_len=0,end_data_len=0;
    
    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        // set header
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, lenght=%d", msg->buffer_len);

        sr->sr_total_write = 0;
        sr->is_begin = true;
        sr->remain_len = 0;

        websocket_cfg.uri = assembleAuthUrl();
        ESP_LOGE(TAG,"websocket_cfg.uri:%s", websocket_cfg.uri);
        client = esp_websocket_client_init(&websocket_cfg);
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

        esp_websocket_client_start(client);
        xTimerStart(shutdown_signal_timer, portMAX_DELAY);
            if (esp_websocket_client_is_connected(client)) {
                ESP_LOGI(TAG, "esp_websocket_client_is_connected()");
               //esp_websocket_client_send(client, data, len, portMAX_DELAY);
           }
            // vTaskDelay(1000 / portTICK_RATE_MS);
        xSemaphoreTake(shutdown_sema, portMAX_DELAY);      
     
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_ON_REQUEST) {
            //ESP_LOGI(TAG, "HTTP_STREAM_ON_REQUEST, lenght=%d, begin=%d", msg->buffer_len, sr->is_begin);
            /* Write first chunk */         
            //return _http_write_chunk(http, sr->buffer, sr_begin_len);
            if (msg->buffer_len * 3 / 2 > sr->buffer_size) {
            ESP_LOGE(TAG, "Please use SR Buffer size greeter than %d", msg->buffer_len * 3 / 2);
            return ESP_FAIL;
            }
            //先将msg->buffer中的音频数据复制到sr->buffer + sr->remain_len中
            memcpy(sr->buffer + sr->remain_len, msg->buffer, msg->buffer_len);
            //sr->sr_total_write += msg->buffer_len;
            sr->remain_len += msg->buffer_len;
            //base64把3字节切成4份，每份6bit，再将高两位补0，所以每次3倍取整后余下的1-2个字节放到下一次
            int keep_next_time = sr->remain_len % 3;
            sr->remain_len -= keep_next_time;
            pre_data_len=0;
            middle_data_len=0;
            end_data_len=0;
            //开始，中间和结束的数据包不一样
            if (sr->is_begin) {
                    sr->is_begin = false;
                    if (sr->on_begin) {
                        sr->on_begin(sr);
                    }
                    pre_data_len = snprintf(sr->b64_buffer,sr->buffer_size,FIRST_PACKET_PRE_DATA,CONFIG_Xunfei_APPID);
            } else{  // if (sr->is_begin) {               
                 //pre_data_len = snprintf(sr->b64_buffer,sr->buffer_size,,Xunfei_APPID);
                  pre_data_len=strlen(MIDDLE_PACKET_PRE_DATA);
                  memcpy(sr->b64_buffer ,MIDDLE_PACKET_PRE_DATA,pre_data_len);
            }
            
            if (mbedtls_base64_encode((unsigned char *)sr->b64_buffer + pre_data_len, sr->buffer_size,  &middle_data_len, (unsigned char *)sr->buffer, sr->remain_len) != 0) {
                ESP_LOGE(TAG, "Error encode b64");
                return ESP_FAIL;
            }
            
            //每个包的尾部
            //end_data_len== snprintf(sr->b64_buffer,sr->buffer_size,FIRST_PACKET_PRE_DATA,Xunfei_APPID);
            end_data_len=strlen(PACKET_END_DATA);
            memcpy(sr->b64_buffer +pre_data_len+middle_data_len,PACKET_END_DATA,end_data_len);

            
            sr->sr_total_write+= sr->remain_len;
            if (keep_next_time > 0) {
                memcpy(sr->buffer, sr->buffer + sr->remain_len, keep_next_time);
            }
            sr->remain_len = keep_next_time;
            ESP_LOGD(TAG, "Total bytes written: %d", sr->sr_total_write);
            //   printf("Total bytes written: %d\n",  sr->sr_total_write);
            need_write=pre_data_len + middle_data_len + end_data_len;
            ESP_LOGI(TAG, "sr->b64_buffer1: %s",sr->b64_buffer);
            if (esp_websocket_client_is_connected(client)) {
            //      esp_websocket_client_send(client, sr->b64_buffer,need_write, portMAX_DELAY);      
            }            
        
        return need_write; 
    }
  
    // Write End chunk 
  
    if (msg->event_id == HTTP_STREAM_POST_REQUEST) {
        
        need_write = 0;
       // if (sr->remain_len) {

             pre_data_len=strlen(LAST_PACKET_PRE_DATA);
             memcpy(sr->b64_buffer ,LAST_PACKET_PRE_DATA,pre_data_len);        

             
            if (mbedtls_base64_encode((unsigned char *)sr->b64_buffer + pre_data_len, sr->buffer_size,  &middle_data_len, (unsigned char *)sr->buffer, sr->remain_len) != 0) {
                ESP_LOGE(TAG, "Error encode b64");
                return ESP_FAIL;
            }
            
            //end_data_len== snprintf(sr->b64_buffer,sr->buffer_size,FIRST_PACKET_PRE_DATA,Xunfei_APPID);
            end_data_len=strlen(PACKET_END_DATA);
            memcpy(sr->b64_buffer +pre_data_len+middle_data_len,PACKET_END_DATA,end_data_len);
            need_write = pre_data_len + middle_data_len + end_data_len;
             ESP_LOGI(TAG, "sr->b64_buffer2: %s",sr->b64_buffer);
             if (esp_websocket_client_is_connected(client)) {
               //   esp_websocket_client_send(client, sr->b64_buffer,need_write, portMAX_DELAY);      
              }     

        return need_write;    
    }

    if (msg->event_id == HTTP_STREAM_FINISH_REQUEST) {
/*
        int read_len = esp_http_client_read(http, (char *)sr->buffer, sr->buffer_size);
        ESP_LOGI(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST, read_len=%d", read_len);
        if (read_len <= 0) {
            return ESP_FAIL;
        }
        if (read_len > sr->buffer_size - 1) {
            read_len = sr->buffer_size - 1;
        }
        sr->buffer[read_len] = 0;
        ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)sr->buffer);
        if (sr->response_text) {
            free(sr->response_text);
        }
        sr->response_text = json_get_token_value(sr->buffer, "result");
        return ESP_OK;
        */
        ESP_LOGE(TAG,"HTTP_STREAM_FINISH_REQUEST");
    }
    
    return ESP_OK;
}

baidu_sr_handle_t baidu_sr_init(baidu_sr_config_t *config)
{
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    baidu_sr_t *sr = calloc(1, sizeof(baidu_sr_t));
    AUDIO_MEM_CHECK(TAG, sr, return NULL);
    sr->pipeline = audio_pipeline_init(&pipeline_cfg);

    sr->buffer_size = config->buffer_size;
    if (sr->buffer_size <= 0) {
        sr->buffer_size = DEFAULT_SR_BUFFER_SIZE;
    }

    sr->buffer = malloc(sr->buffer_size);
    AUDIO_MEM_CHECK(TAG, sr->buffer, goto exit_sr_init);
    sr->b64_buffer = malloc(sr->buffer_size +240);//要添加附加的参数
    AUDIO_MEM_CHECK(TAG, sr->b64_buffer, goto exit_sr_init);
    sr->format = strdup(config->format);
    AUDIO_MEM_CHECK(TAG, sr->format, goto exit_sr_init);
    sr->token = strdup(config->token);
    AUDIO_MEM_CHECK(TAG, sr->token, goto exit_sr_init);
    sr->cuid = strdup(config->cuid);
    AUDIO_MEM_CHECK(TAG, sr->cuid, goto exit_sr_init);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.out_rb_size=81920;
    sr->i2s_reader = i2s_stream_init(&i2s_cfg);

    http_stream_cfg_t http_cfg = {
        .type = AUDIO_STREAM_WRITER,
        .event_handle = _http_stream_writer_event_handle,
        .user_data = sr,
        .task_stack = BAIDU_SR_TASK_STACK,
        .task_core=1,
        //.out_rb_size=40960,
    };
    sr->http_stream_writer = http_stream_init(&http_cfg);
    sr->sample_rates = config->record_sample_rates;
    //sr->encoding = config->encoding;
    sr->on_begin = config->on_begin;

    audio_pipeline_register(sr->pipeline, sr->http_stream_writer, "sr_http");
    audio_pipeline_register(sr->pipeline, sr->i2s_reader,         "sr_i2s");
    audio_pipeline_link(sr->pipeline, (const char *[]) {"sr_i2s", "sr_http"}, 2);
    i2s_stream_set_clk(sr->i2s_reader, config->record_sample_rates, 16, 1);

    return sr;
exit_sr_init:
    baidu_sr_destroy(sr);
    return NULL;
}

esp_err_t baidu_sr_destroy(baidu_sr_handle_t sr)
{
    if (sr == NULL) {
        return ESP_FAIL;
    }
    audio_pipeline_terminate(sr->pipeline);
    audio_pipeline_remove_listener(sr->pipeline);
    audio_pipeline_deinit(sr->pipeline);
    audio_element_deinit(sr->i2s_reader);
    audio_element_deinit(sr->http_stream_writer);
    free(sr->buffer);
    free(sr->b64_buffer);
    free(sr->format);
    free(sr->cuid);
    free(sr->token);
    free(sr);
    return ESP_OK;
}

esp_err_t baidu_sr_set_listener(baidu_sr_handle_t sr, audio_event_iface_handle_t listener)
{
    if (listener) {
        audio_pipeline_set_listener(sr->pipeline, listener);
    }
    return ESP_OK;
}

esp_err_t baidu_sr_start(baidu_sr_handle_t sr)
{
   // snprintf(sr->buffer, sr->buffer_size, BAIDU_SR_ENDPOINT, sr->api_key);
    audio_element_set_uri(sr->http_stream_writer, BAIDU_SR_ENDPOINT);//sr->buffer);
    audio_pipeline_reset_items_state(sr->pipeline);
    audio_pipeline_reset_ringbuffer(sr->pipeline);
    audio_pipeline_run(sr->pipeline);
    return ESP_OK;
}

char *baidu_sr_stop(baidu_sr_handle_t sr)
{
    audio_pipeline_stop(sr->pipeline);
    ESP_LOGI(TAG, "baidu_sr_stop 1");
    audio_pipeline_wait_for_stop(sr->pipeline);
    ESP_LOGI(TAG, "baidu_sr_stop 2");
    return sr->response_text;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    tcpip_adapter_init();

    ESP_LOGI(TAG, "[ 0 ] Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    
    // Initialize Button peripheral
    periph_button_cfg_t btn_cfg = {
        .gpio_mask = (1ULL << get_input_mode_id()) | (1ULL << get_input_rec_id()),
    };
    esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);

    periph_led_cfg_t led_cfg = {
        .led_speed_mode = LEDC_LOW_SPEED_MODE,
        .led_duty_resolution = LEDC_TIMER_10_BIT,
        .led_timer_num = LEDC_TIMER_0,
        .led_freq_hz = 5000,
    };
    led_handle = periph_led_init(&led_cfg);

    // Start wifi & button peripheral
    esp_periph_start(set, button_handle);
    esp_periph_start(set, led_handle);

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

        
     get_time_from_net();
    
    
    //ESP_LOGE(TAG,"baidu_access_token:%s" ,(char *)baidu_access_token);
    baidu_sr_config_t sr_config = {
        .format="pcm",
       // .token=(char *)baidu_access_token,
        .token="24.e29088d370bb70.2592000.1594802208.282335-16147548",
        .cuid="esp32",
        .record_sample_rates = EXAMPLE_RECORD_PLAYBACK_SAMPLE_RATE,
        .on_begin = baidu_sr_begin,
    };
   
     baidu_sr_handle_t sr = baidu_sr_init(&sr_config);
 
    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from the pipeline");
    baidu_sr_set_listener(sr, evt); 

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Listen for all pipeline events");
    ESP_LOGE(TAG, "AUDIO_ELEMENT_TYPE_ELEMENT:%x",AUDIO_ELEMENT_TYPE_ELEMENT);
    ESP_LOGE(TAG, "AUDIO_ELEMENT_TYPE_PERIPH:%x",AUDIO_ELEMENT_TYPE_PERIPH);
    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) {
            ESP_LOGW(TAG, "[ * ] Event process failed: src_type:0x%x, source:%p cmd:%d, data:%p, data_len:%d",msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
            continue;
        }

        ESP_LOGI(TAG, "[ * ] Event received: src_type:0x%x, source:%p cmd:%d, data:%p, data_len:%d",msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);

        if (msg.source_type != PERIPH_ID_BUTTON) {
            continue;
        }

        // It's MODE button
        if ((int)msg.data == get_input_mode_id()) {
           // break;
        }

        if ((int)msg.data != get_input_rec_id()) {
            continue;
        }

        if (msg.cmd == PERIPH_BUTTON_PRESSED) {         
            ESP_LOGI(TAG, "[ * ] Resuming pipeline");
            baidu_sr_start(sr);
        } else if (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE) {
            ESP_LOGI(TAG, "[ * ] Stop pipeline");

            periph_led_stop(led_handle, get_green_led_gpio());

            char *original_text = baidu_sr_stop(sr);
            if (original_text == NULL) {
                continue;
            }
            ESP_LOGI(TAG, "Original text = %s", original_text);
           // char *translated_text = baidu_translate(original_text, BAIDU_TRANSLATE_LANG_FROM, BAIDU_TRANSLATE_LANG_TO, CONFIG_BAIDU_API_KEY);
          //  if (translated_text == NULL) {
          //      continue;
         //  }
          //  ESP_LOGI(TAG, "Translated text = %s", translated_text);
         
        }//else if
   
    }//while(1)
    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
     
    baidu_sr_destroy(sr);
   
   
    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);
    esp_periph_set_destroy(set);  
}