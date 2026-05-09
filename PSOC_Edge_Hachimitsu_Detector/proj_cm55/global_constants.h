/*
 * global_constants.h
 *
 *  Created on: 2025骞?1鏈?0鏃?
 *      Author: 14838
 */

#ifndef GLOBAL_CONSTANTS_H_
#define GLOBAL_CONSTANTS_H_

/******************************************************************************
 * 鐚彨浠诲姟鐨勫畯甯搁噺
 *****************************************************************************/
#define SAMPLE_RATE             16000           // 閲囨牱鐜?
#define STEP_SIZE_MS            192             // 姝ラ暱姣
#define WINDOW_SIZE_MS          832             // 绐楀彛姣
#define STEP_SIZE_SEC           (STEP_SIZE_MS / 1000.0f)          // 姝ラ暱
#define OUTPUT_THRESHOLD_SCORE  0.6f            // 鐚彨闃堝€?
#define WINDOW_SIZE             (WINDOW_SIZE_MS / 1000.0f)          // 绐楀彛澶у皬
#define DETECTION_REPORT_COOLDOWN_SEC 5.0f      // 杩炵画鍛戒腑鏃剁殑涓婃姤鍐峰嵈鏃堕棿
#define IMAGE_CAPTURE_COOLDOWN_SEC 15.0f        // 楂樺儚绱犲浘鐗囪姹傚崟鐙檺棰?
 
// #define WINDOW_SIZE_SAMPLES     ((int)(SAMPLE_RATE * WINDOW_SIZE))
#define WINDOW_SIZE_SAMPLES     ((SAMPLE_RATE * WINDOW_SIZE_MS + 500) / 1000)
#define STEP_SIZE_SAMPLES       ((SAMPLE_RATE * STEP_SIZE_MS + 500) / 1000)
#define DETECTION_REPORT_COOLDOWN_WINDOWS ((int)(DETECTION_REPORT_COOLDOWN_SEC / STEP_SIZE_SEC + 0.5f))
#define IMAGE_CAPTURE_COOLDOWN_WINDOWS ((int)(IMAGE_CAPTURE_COOLDOWN_SEC / STEP_SIZE_SEC + 0.5f))

#define MODEL_INPUT_LEN         16000
#define MODEL_OUTPUT_LEN        1

#endif /* GLOBAL_CONSTANTS_H_ */
