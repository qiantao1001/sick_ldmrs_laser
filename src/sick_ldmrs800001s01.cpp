/*
 * Copyright (C) 2015, DFKI GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Osnabrück University nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *  Created on: 20.11.2015
 *
 *      Authors:
 *         Martin Günther <martin.guenther@dfki.de>
 *         Jochen Sprickerhof <jochen@sprickerhof.de>
 *
 */

#include <iostream>

#include "sick_ldmrs_driver/sick_ldmrs800001s01.hpp"

#include <sick_ldmrs/datatypes/EvalCaseResults.hpp>
#include <sick_ldmrs/datatypes/EvalCases.hpp>
#include <sick_ldmrs/datatypes/Fields.hpp>
#include <sick_ldmrs/datatypes/Measurement.hpp>
#include <sick_ldmrs/datatypes/Msg.hpp>
#include <sick_ldmrs/datatypes/Object.hpp>
#include <sick_ldmrs/datatypes/Scan.hpp>

#include <sick_ldmrs/devices/LD_MRS.hpp>

#include <sick_ldmrs/tools/errorhandler.hpp>
#include <sick_ldmrs/tools/toolbox.hpp>


namespace sick_ldmrs_driver
{

SickLDMRS::SickLDMRS(Manager *manager)
  : application::BasicApplication()
  , manager_(manager)
{
  dynamic_reconfigure::Server<SickLDMRSDriverConfig>::CallbackType f;
  f = boost::bind(&SickLDMRS::update_config, this, _1, _2);
  dynamic_reconfigure_server_.setCallback(f);
}

void SickLDMRS::setData(BasicData &data)
{
  //
  // Do something with it.
  //
  // Here, we just print what we've got.
  //
  std::string datatypeStr;
  std::string sourceIdStr;

  switch (data.getDatatype())
  {
  case Datatype_Scan:
    datatypeStr = "Scan (" + ::toString(((Scan&)data).getNumPoints()) + " points)";
    {
      // Print the scan start timestamp (NTP time)
      Scan* scan = dynamic_cast<Scan*>(&data);
      std::vector<ScannerInfo> scannerInfos = scan->getScannerInfos();
      std::vector<ScannerInfo>::const_iterator it;
      for (it = scannerInfos.begin(); it != scannerInfos.end(); ++it)
      {
        const Time& time = it->getStartTimestamp();
        ROS_INFO("LdmrsApp::setData(): Scan start time: %s", time.toString().c_str());
      }

      // TODO MG: convert to PointCloud, publish
    }
    break;
  case Datatype_Objects:
    datatypeStr = "Objects (" + ::toString(((ObjectList&)data).size()) + " objects)";
    break;
  case Datatype_Fields:
    datatypeStr = "Fields (" + ::toString(((Fields&)data).getFields().size()) + " fields, " +
                  ::toString(((Fields&)data).getNumberOfValidFields()) + " of which are valid)";
    break;
  case Datatype_EvalCases:
    datatypeStr = "EvalCases (" + ::toString(((EvalCases&)data).getEvalCases().size()) + " cases)";
    break;
  case Datatype_EvalCaseResults:
    datatypeStr = "EvalCaseResults (" + ::toString(((EvalCaseResults&)data).size()) + " case results)";
    break;
  case Datatype_Msg:
    datatypeStr = "Msg (" + ((Msg&)data).toString() + ")";
    break;
  case Datatype_MeasurementList:
    datatypeStr = "MeasurementList (" + ::toString(((MeasurementList&)data).m_list.size()) + " entries)";
    break;
  default:
    datatypeStr = "(unknown)";
  }

  sourceIdStr = ::toString(data.getSourceId());

  ROS_INFO("LdmrsApp::setData(): Called with data of type %s from ID %s", datatypeStr.c_str(), sourceIdStr.c_str());
}

void SickLDMRS::validate_config(SickLDMRSDriverConfig &conf)
{
  if (conf.start_angle <= conf.end_angle)
  {
    ROS_WARN("Start angle must be greater than end angle. Adjusting start_angle.");
    conf.start_angle = conf.end_angle;  // TODO: - 2 * ticks2rad
  }
}

void SickLDMRS::update_config(SickLDMRSDriverConfig &new_config, uint32_t level)
{
  validate_config(new_config);
  config_ = new_config;

  std::cout << "start_angle:    " << config_.start_angle << std::endl;
  std::cout << "end_angle:      " << config_.end_angle << std::endl;
  std::cout << "frame_id:       " << config_.frame_id << std::endl;
  std::cout << "scan_frequency: " << config_.scan_frequency << std::endl;

  devices::LDMRS* ldmrs;
  ldmrs = dynamic_cast<devices::LDMRS*>(manager_->getFirstDeviceByType(Sourcetype_LDMRS));
  if (ldmrs == NULL)
  {
    ROS_WARN("update_config: no connection to LDMRS!");
    return;
  }

  // TODO: if (new_config.start_angle < config_.end_angle): first update end angle,
  // then start angle to ensure that always start_angle > end_angle; see comments
  // in LuxBase::cmd_setScanAngles().
  ldmrs->setScanAngles(new_config.start_angle, new_config.end_angle);

  switch (config_.scan_frequency)
  {
  case SickLDMRSDriver_ScanFreq1250:
    ldmrs->setScanFrequency(12.5d);
  case SickLDMRSDriver_ScanFreq2500:
    ldmrs->setScanFrequency(25.0d);
  case SickLDMRSDriver_ScanFreq5000:
    ldmrs->setScanFrequency(50.0d);
  default:
    ROS_ERROR("Unknown scan frequency: %i", config_.scan_frequency);
  }
}

} /* namespace sick_ldmrs_driver */


int main(int argc, char **argv)
{
  ros::init(argc, argv, "sick_ldmrs800001s01");

  // The MRS-App connects to an MRS, reads its configuration and receives all incoming data.
  // First, create the manager object. The manager handles devices, collects
  // device data and forwards it to the application(s).
  ROS_INFO("Creating the manager.");
  Manager manager;

  // Add the application. As the devices may send configuration data only once
  // at startup, the applications must be present before the devices are
  // started.
  Sourcetype type;
  std::string name;
  UINT16 id;
  bool result = false;

  ROS_INFO("Adding the application SickLDMRS.");
  name = "Sick LDMRS ROS Driver App";
  id = 1356;

  application::BasicApplication* app;
  app = new sick_ldmrs_driver::SickLDMRS(&manager);
  app->setApplicationName(name);

  result = manager.addApplication(app, id);
  if (result == false)
  {
    ROS_ERROR("Failed to add application %s, aborting!", name.c_str());
    return EXIT_FAILURE;
  }
  ROS_INFO("Application is running.");

  //
  // Add and run the sensor
  //
  // The MRS device could be configured like this:
  // m_weWantScanData:          true
  // m_weWantObjectData:        true
  // m_weWantFieldData:         false
  // m_weWantScanDataFromSopas: false
  ROS_INFO("Adding the LDMRS device.");
  type = Sourcetype_LDMRS;
  name = "LDMRS-1";
  id = 1;
  result = manager.addAndRunDevice(type, name, id);
  if (result == false)
  {
    ROS_ERROR("Failed to add device %s, aborting!", name.c_str());
    return EXIT_FAILURE;
  }

  ROS_INFO("%s is initialized.", ros::this_node::getName().c_str());
  ros::spin();

  return EXIT_SUCCESS;
}
