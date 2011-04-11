#include "Isis.h"

#include <iomanip>

#include "Brick.h"
#include "ControlNet.h"
#include "ControlPoint.h"
#include "Cube.h"
#include "Filename.h"
#include "iException.h"
#include "Latitude.h"
#include "Longitude.h"
#include "SpecialPixel.h"
#include "SurfacePoint.h"
#include "UniversalGroundMap.h"

using namespace std;
using namespace Isis;

enum GetLatLon { Adjusted, Apriori };

void IsisMain() {
  UserInterface &ui = Application::GetUserInterface();
  ControlNet cnet(ui.GetFilename("CNET"));

  // Get input cube and get camera model for it
  string from = ui.GetFilename("MODEL");
  Cube cube;
  cube.Open(from);
  UniversalGroundMap *ugm = NULL;
  try {
    ugm = new UniversalGroundMap(cube);
  }
  catch(iException e) {
    iString msg = "Cannot initalize UniversalGroundMap for cube [" + from + "]";
    throw iException::Message(iException::User, msg, _FILEINFO_);
  }

  GetLatLon newRadiiSource;
  iString getLatLon = iString(ui.GetAsString("GETLATLON")).UpCase();
  if (getLatLon == "ADJUSTED") {
      newRadiiSource = Adjusted;
  }
  else if (getLatLon == "APRIORI") {
      newRadiiSource = Apriori;
  }
  else {
      string msg = "The value for parameter GETLATLON [";
      msg += ui.GetAsString("GETLATLON") + "] must be provided.";
      throw iException::Message(iException::User, msg, _FILEINFO_);
  }

  int numSuccesses = 0;
  int numFailures = 0;
  string failedIDs = "";

  for(int i = 0; i < cnet.GetNumPoints() ; i++) {
    ControlPoint *cp = cnet.GetPoint(i);

    if(cp->GetType() == ControlPoint::Ground) {
      // Create Brick on samp, line to get the dn value of the pixel
      SurfacePoint surfacePt;
      if (newRadiiSource == Adjusted)
        surfacePt = cp->GetAdjustedSurfacePoint();
      else if (newRadiiSource == Apriori)
        surfacePt = cp->GetAprioriSurfacePoint();
      bool success = surfacePt.Valid();

      if(success) {
        success = ugm->SetUniversalGround(surfacePt.GetLatitude().GetDegrees(),
            surfacePt.GetLongitude().GetDegrees());
      }

      Brick b(1, 1, 1, cube.PixelType());
      if(success) {
        b.SetBasePosition((int)ugm->Sample(), (int)ugm->Line(), 1);
        cube.Read(b);
        success = !IsSpecial(b[0]);
      }

      // if we are unable to calculate a valid Radius value for a point,
      // we will ignore this point and keep track of it
      if(!success) {
        numFailures++;
        if(numFailures > 1) {
          failedIDs = failedIDs + ", ";
        }
        failedIDs = failedIDs + cp->GetId();
        cp->SetIgnored(true);
      }
      // otherwise, we will replace the computed radius value to the output control net
      else {
        numSuccesses++;
        surfacePt.ResetLocalRadius(Distance(b[0], Distance::Meters));
        if (newRadiiSource == Adjusted)
          cp->SetAdjustedSurfacePoint(surfacePt);
        else if (newRadiiSource == Apriori)
          cp->SetAprioriSurfacePoint(surfacePt);
      }
    }
  }

  delete ugm;
  ugm = NULL;

  if(numSuccesses == 0) {
    string msg = "No valid radii can be calculated. Verify that the DEM [" + ui.GetAsString("MODEL") + "] is valid.";
    throw Isis::iException::Message(Isis::iException::User, msg, _FILEINFO_);
  }

  cnet.Write(ui.GetFilename("TO"));

  // Write results to Logs
  // Summary group is created with the counts of successes and failures
  PvlGroup summaryGroup = PvlGroup("Summary");
  summaryGroup.AddKeyword(PvlKeyword("Successes", numSuccesses));
  summaryGroup.AddKeyword(PvlKeyword("Failures", numFailures));

  bool errorlog;
  Filename errorlogFile;
  // if a filename was entered, use it to create the log
  if(ui.WasEntered("ERRORS")) {
    errorlog = true;
    errorlogFile = ui.GetFilename("ERRORS");
  }
  // if no filename was entered, but there were some failures,
  // create an error log named "failures" in the current directory
  else if(numFailures > 0) {
    errorlog = true;
    errorlogFile = "failures.log";
  }
  // if all radii are successfully calculated and no error
  // file is named, only output summary to application log
  else {
    errorlog = false;
  }

  if(errorlog) {
    // Write details in error log
    Pvl results;
    results.SetName("Results");
    results.AddGroup(summaryGroup);
    if(numFailures > 0) {
      // if there are any failures, add comment to the summary log to alert user
      summaryGroup.AddComment("Unable to calculate radius for all points. Point IDs for failures contained in [" + errorlogFile.Name() + "].");
      PvlGroup failGroup = PvlGroup("Failures");
      failGroup.AddComment("A point fails if we are unable to set universal ground or if the radius calculated is a special pixel value.");
      failGroup.AddKeyword(PvlKeyword("PointIDs", failedIDs));
      results.AddGroup(failGroup);
    }
    results.Write(errorlogFile.Expanded());
  }
  // Write summary to application log
  Application::Log(summaryGroup);
}
