#include "Isis.h"
#include "ProcessByTile.h"
#include "SpecialPixel.h"
#include "Pvl.h"
#include "UserInterface.h"
#include "Brick.h"
#include "Apollo.h"
#include <string>
#include <cstdlib>

using namespace std;
using namespace Isis;

void cpy(Buffer &in, Buffer &out);
static int dim;
static bool resvalid;
static string action;

void IsisMain() {
  // We will be processing by line
  ProcessByTile p;
  p.SetTileSize(128, 128);

  // Setup the input and output cubes
  Cube* info = p.SetInputCube("FROM");
  PvlKeyword &status = info ->GetGroup("RESEAUS")["STATUS"];
  UserInterface &ui = Application::GetUserInterface();
  string in = ui.GetFilename("FROM");
  
  string spacecraft = (info->GetGroup("Instrument")["SpacecraftName"]);
  string instrument = (info->GetGroup("Instrument")["InstrumentId"]);
  Apollo apollo(spacecraft, instrument);
  if (spacecraft.substr(0,6) != "APOLLO") {
    string msg = "This application is for use with Apollo spacecrafts only. ";
    throw Isis::iException::Message(Isis::iException::Pvl,msg, _FILEINFO_);
  }

  // Check reseau status and make sure it is not nominal or removed
  if ((string)status == "Nominal") {
    string msg = "Input file [" + in + 
          "] appears to have nominal reseau status. You must run findrx first.";
    throw iException::Message(iException::User,msg, _FILEINFO_);
  }
  if ((string)status == "Removed") {
    string msg = "Input file [" + in + 
          "] appears to already have reseaus removed.";
    throw iException::Message(iException::User,msg, _FILEINFO_);
  }  

  status = "Removed";

  p.SetOutputCube ("TO");
  
  // Start the processing
  p.StartProcess(cpy);
  p.EndProcess();

  dim = apollo.ReseauDimension();

  // Get other user entered options
  string out= ui.GetFilename("TO");
  resvalid = ui.GetBoolean("RESVALID");
  action = ui.GetString("ACTION");

  // Open the output cube
  Cube cube;
  cube.Open(out, "rw");

  PvlGroup &res = cube.Label()->FindGroup("RESEAUS",Pvl::Traverse);

  // Get reseau line, sample, type, and valid Keywords
  PvlKeyword lines = res.FindKeyword("LINE");
  PvlKeyword samps = res.FindKeyword("SAMPLE");
  PvlKeyword type = res.FindKeyword("TYPE");
  PvlKeyword valid = res.FindKeyword("VALID");
  int numres = lines.Size();

  Brick brick(dim,dim,1,cube.PixelType());
  int width = ui.GetInteger("WIDTH");
  for (int res=0; res<numres; res++) {
    if ((resvalid == 0 || (int)valid[res] == 1)) {
      int baseSamp = (int)((double)samps[res]+0.5) - (dim/2);
      int baseLine = (int)((double)lines[res]+0.5) - (dim/2);
      brick.SetBasePosition(baseSamp,baseLine,1);
      cube.Read(brick);
      if (action == "NULL") {
        // set the three pixels surrounding the reseau to null
        for (int i=0; i<dim; i++) {
          for (int j=(width-1)/-2; j<=width/2; j++) {
            // vertical lines
            brick[dim*i+dim/2+j] = Isis::Null;
            // horizontal lines
            brick[dim*(dim/2+j)+i] = Isis::Null;
          }
        }
      }
      else if (action == "PATCH") {
        for (int i = 0; i < dim; i++) {
          for (int j=(width-1)/-2; j<=width/2; j++) {
            // vertical lines
            brick[dim*i+dim/2+j] = (brick[dim*i+dim/2-width+j] + brick[dim*i+dim/2+width+j])/2.0;
            // horizontal lines
            brick[dim*(dim/2+j)+i] = (brick[dim*(dim/2-width+j)+i]+brick[dim*(dim/2+width+j)+i])/2.0;
          }
        }
      }
    }
    cube.Write(brick);
  }
  cube.Close();
}

// Copy the input cube to the output cube
void cpy(Buffer &in, Buffer &out) {
  for (int i=0; i<in.size(); i++) {
    out[i] = in[i];
  }
}
