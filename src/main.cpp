/******************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Bluewhale Robot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Author: Xie fusheng, Randoms
 *******************************************************************************/

#include "bw_auto_dock/AsyncSerial.h"

#include <iostream>
#include <boost/thread.hpp>

#include <ros/ros.h>
#include <ros/package.h>
#include "bw_auto_dock/DockController.h"
#include "bw_auto_dock/StatusPublisher.h"
#include "bw_auto_dock/getDockPosition.h"

using namespace std;

int main(int argc, char** argv)
{
    ros::init(argc, argv, "bw_auto_dock_server");
    ros::start();

    //获取串口参数
    std::string port;
    ros::param::param<std::string>("~port", port, "/dev/ttyUSB004");
    int baud;
    ros::param::param<int>("~baud", baud, 115200);
    ROS_INFO_STREAM("port:" << port << " baud:" << baud);
    //获取小车机械参数
    double back_distance = 0;
    ros::param::param<double>("~back_distance", back_distance, 0.30);
    bw_auto_dock::StatusPublisher bw_status;

    //获取小车控制参数
    double max_linearspeed, max_rotspeed;

    ros::param::param<double>("~max_linearspeed", max_linearspeed, 0.2);
    ros::param::param<double>("~max_rotspeed", max_rotspeed, 0.2);

    double kp, ki, kd;
    ros::param::param<double>("~back_dock_kp", kp, 0.2);
    ros::param::param<double>("~back_dock_ki", ki, 0.04);
    ros::param::param<double>("~back_dock_kd", kd, 0.0);

    std::string odom_frame_id;
    ros::param::param<std::string>("~odom_frame_id", odom_frame_id, "odom");

    std::string station_filename;
    ros::param::param<std::string>("~station_filename", station_filename, "dock_station.txt");

    double grid_length;
    ros::param::param<double>("~grid_length", grid_length, 4.0);

    try
    {
        CallbackAsyncSerial serial(port, baud);

        serial.setCallback(boost::bind(&bw_auto_dock::StatusPublisher::Update, &bw_status, _1, _2));

        bw_auto_dock::DockController bw_controler(back_distance, max_linearspeed, max_rotspeed, &bw_status, &serial);
        boost::thread bw_controlerThread(&bw_auto_dock::DockController::run, &bw_controler);
        bw_controler.setDockPid(kp, ki, kd);

        //计算充电桩位置
        bw_auto_dock::CaculateDockPosition caculate_DockPosition(grid_length, odom_frame_id, station_filename,
                                                                 &bw_controler, &bw_status);
        boost::thread caculate_DockPositionThread(&bw_auto_dock::CaculateDockPosition::run, &caculate_DockPosition);
        bw_controler.setDockPositionCaculate(&caculate_DockPosition);

        // send reset cmd
        char resetCmd[] = { (char)0xcd, (char)0xeb, (char)0xd7, (char)0x01, 'I' };
        serial.write(resetCmd, 5);

        ros::Rate r(30);  //发布周期为50hz
        while (ros::ok())
        {
            if (serial.errorStatus() || serial.isOpen() == false)
            {
                ROS_ERROR_STREAM("Error: serial port closed unexpectedly");
                break;
            }
            bw_status.Refresh();  //定时发布状态
            bw_controler.dealing_status();
            r.sleep();
        }

    quit:
        serial.close();
    }
    catch (std::exception& e)
    {
        ROS_ERROR_STREAM("Exception: " << e.what());
    }

    ros::shutdown();
    return 0;
}
