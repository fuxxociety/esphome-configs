#include "esphome.h"
using namespace std;

// Declare functions before calling them.
bool scheduled_runtime(string);
string update_next_runtime(string, string);

bool scheduled_runtime(string time) {
  // Retrieve the current time.
  auto time_now = id(homeassistant_time).now();
  int time_hour = time_now.hour;
  int time_minute = time_now.minute;
  int time_wday = time_now.day_of_week; //added for day scheduling

  //Prevent program crash - functions were created expecting a time formated string
  // if you pass "now", program crashes in certain cases.
  if (time == "now") { //"now" in English, customize according to your prefs.
    return false;
  }

  // Split the hour and minutes.
  int next_hour = atoi(time.substr(0,2).c_str());
  int next_minute = atoi(time.substr(3,2).c_str());  int next_wday = 0;
  string day = time.substr(6,3).c_str(); //day text is added to original string

// Converting days to week numbers, change text based on your Language
    if (day == "Mon" || day == "mon" || day == "MON") {
      next_wday = 2;
    } else if (day == "Tue" || day == "tue" || day == "TUE") {
      next_wday = 3;
    } else if (day == "Wed" || day == "wed" || day == "WED") {
      next_wday = 4;
    } else if (day == "Thu" || day == "thu" || day == "THU") {
      next_wday = 5;
    } else if (day == "Fri" || day == "fri" || day == "FRI") {
      next_wday = 6;
    } else if (day == "Sat" || day == "sat" || day == "SAT") {
      next_wday = 7;
    } else if (day == "Sun" || day == "sun" || day == "SUN") {
      next_wday = 1;
    } else if (day == "Tod") { //Today in English
      next_wday = time_wday;
    }

  //ESP_LOGD("scheduled_runtime()", "now: %i:%i, wday: %i", next_hour, next_minute, time_wday);
  // return (time_hour == next_hour && time_minute == next_minute);
  return (time_hour == next_hour && time_minute == next_minute && time_wday == next_wday); //added wday to condition
}

string update_next_runtime(string time_list, string days_list) {
  // Initialize variables.
  vector<string> times;
  vector<string> next_time;
  vector<string> days;      //added for day scheduling
  vector<string> next_day; //added for day scheduling
  char * token;
  char * token2;   //to work on day string
  //bool single_time = false;
  //bool single_day = false;
  string updated_next_time;
  string updated_next_day;

  // Split the list of run times into an array.
  token = strtok(&time_list[0], ",");
  while (token != NULL) {
    times.push_back(token);
    token = strtok(NULL, ",");
  }
  // Split the list of run days into an array.
  token2 = strtok(&days_list[0], ",");
  while (token2 != NULL) {
    days.push_back(token2);
    token2 = strtok(NULL, ",");
  }

  // Need to delete this in order to day-time integration works
  // Stop now if the list does not contain more than one time.
  //if (times.size() <= 1) {
    //return time_list;
    //updated_next_time = time_list;
    //single_time = true;
  //}
  // Stop now if the list does not contain more than one day.
  //if (days.size() <= 1) {
    //updated_next_day = days_list;
    //single_day = true;
  //}

  // Retrieve the current time.
  auto time_now = id(homeassistant_time).now();
  int time_hour = time_now.hour;
  int time_minute = time_now.minute;
  int time_wday = time_now.day_of_week;

  // Initialize variables.
  int next_hour = 0;
  int next_minute = 0;
  int index = 0;
  int loop_count = 0;
  int time_count = times.size()-1;

  // Compare the list of times with the current time, and return the next in the list.
  //ESP_LOGD("update_next_runtime", "now: %i:%i", hour, minute);
  //if (!single_time) {
  for (string time : times) {
    // Retrieve the next scheduled time from the list.
    next_hour = atoi(time.substr(0,2).c_str());
    next_minute = atoi(time.substr(3,2).c_str());

    //ESP_LOGD("update_next_runtime", "next_hour: %s", time.c_str());
    if (time_hour < next_hour || (time_hour == next_hour && time_minute < next_minute)) {
      // Return this time if the next hour is greater than the current hour.
      //return times[loop_count].c_str();
      //break;
      updated_next_time = times[loop_count].c_str();
      break;
    // When we reach the end of our schedule for the day, return the first time of tomorrow.
    } else if (time_count == loop_count) {
      //return times[0].c_str();
      //break;
      updated_next_time = times[0].c_str();
      break;
    }

    // Increment the loop counter and array index.
    loop_count += 1;
    index += 2;
  }
  //}

  int loop2_count = 0;
  int day_count = days.size()-1;
  int next_wday = 0;
  int index2 = 0;

  //if (!single_day) {
  for (string day : days) {
    // Retrieve the next scheduled day from the list. Set your preferred language. Check correct correlations with day numbers
    if (day == "Mon" || day == "mon" || day == "MON") {
      next_wday = 2;
    } else if (day == "Tue" || day == "tue" || day == "TUE") {
      next_wday = 3;
    } else if (day == "Wed" || day == "wed" || day == "WED") {
      next_wday = 4;
    } else if (day == "Thu" || day == "thu" || day == "THU") {
      next_wday = 5;
    } else if (day == "Fri" || day == "fri" || day == "FRI") {
      next_wday = 6;
    } else if (day == "Sat" || day == "sat" || day == "SAT") {
      next_wday = 7;
    } else if (day == "Sun" || day == "sun" || day == "SUN") {
      next_wday = 1;
    }

    //ESP_LOGD("update_next_runtime", "next_hour: %s", time.c_str());
    if (time_wday == next_wday && (time_hour < next_hour || (time_hour == next_hour && time_minute < next_minute))) {
      // Return this day if the next day is today AND there is still a scheduled time for today.
      //updated_next_day = days[loop2_count].c_str();
      updated_next_day = "Tod"; //Today
      break;
      // If the next day is not today, also the next time is the first of day
    } else if (time_wday < next_wday) {
      updated_next_day = days[loop2_count].c_str();      updated_next_time = times[0].c_str();
      break;
      // When we reach the end of our schedule for the week, return the first day of next week.
    } else if (day_count == loop2_count) {
      updated_next_day = days[0].c_str();
      break;
    }

    // Increment the loop counter and array index.
    loop2_count += 1;
    index2 += 2;
  }
  //}

  return updated_next_time + " " + updated_next_day;
}
