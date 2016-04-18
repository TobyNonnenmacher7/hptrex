// eddy
#include "TTPCTRExPatAlgorithm.hxx"

trex::TTPCTRExPatAlgorithm::TTPCTRExPatAlgorithm() {
  // has no hits by default
  fDriftVelocity = 0;
  fHasHits = false;

  // initial values
  fMasterLayout = 0;
  fMasterVolGroupMan = 0;
}

trex::TTPCTRExPatAlgorithm::~TTPCTRExPatAlgorithm(){
  // clear values from last lot of processing
  CleanUp();
}

void trex::TTPCTRExPatAlgorithm::CleanUp(){

  // delete stuff from previous processing
  fSubAlgorithms.clear();

  // delete all unit volumes
  for(auto mapIter=fMasterHitMap.begin();mapIter!=fMasterHitMap.end();++mapIter){
    delete mapIter->second;
  }
  fMasterHitMap.clear();

  // delete stuff for whole event
  if(fMasterLayout) 
  {
    delete fMasterLayout;
    fMasterLayout = 0;
  }
  if(fMasterVolGroupMan)
  {
    delete fMasterVolGroupMan;
    fMasterVolGroupMan = 0;
  }

  fDeltaHits.clear();
  fHits.clear();
}

void trex::TTPCTRExPatAlgorithm::PrepareHits(std::vector<trex::TTPCHitPad*>& hits){
  // needed for pattern recognition - min and max times either side of the cathode
  double tPMin = +99999999.;
  double tPMax = -99999999.;
  double tNMin = +99999999.;
  double tNMax = -99999999.;
  fHits=hits;
  for (std::vector<trex::TTPCHitPad*>::iterator hit = hits.begin(); hit != hits.end(); ++hit){
    trex::TTPCHitPad* phit = *hit;

    //MDH TODO - FIX CALL TO GEOM
    // sense determines which side of the cathode we're on
    //    int curX = int(trex::TGeomInfo::TPC().GetDriftSense(phit->GetGeomId()));
    int curX=0;

    // needed for pattern recognition - get min and max times either side of the cathode
    std::vector<double> peakTimes = phit->GetPeakTimes();
    if(peakTimes.size()){
      for(std::vector<double>::iterator peakTimeIt = peakTimes.begin(); peakTimeIt != peakTimes.end(); ++peakTimeIt){
        if(curX < 0){
          tNMin = std::min(tNMin, *peakTimeIt);
          tNMax = std::max(tNMax, *peakTimeIt);
        }
        else{
          tPMin = std::min(tPMin, *peakTimeIt);
          tPMax = std::max(tPMax, *peakTimeIt);
        };
      };
    }
    else{
      if(curX < 0){
        tNMin = std::min(tNMin, phit->GetTime());
        tNMax = std::max(tNMax, phit->GetTime());
      }
      else{
        tPMin = std::min(tPMin, phit->GetTime());
        tPMax = std::max(tPMax, phit->GetTime());
      };
    };
  };

  // add maximum and minimum times either side of the cathode
  fMasterLayout->SetTimeRanges(tNMin, tNMax, tPMin, tPMax);

  // work out minimum and maximum cell ids in x, y and z
  int minX = +99999;
  int maxX = -99999;
  int minY = +99999;
  int maxY = -99999;
  int minZ = +99999;
  int maxZ = -99999;
  for (std::vector<trex::TTPCHitPad*>::iterator hitIt = hits.begin(); hitIt != hits.end(); ++hitIt){
    trex::TTPCHitPad* hit = *hitIt;

    //MDH
    //Hopefully our hit object will have a GetTime method or else we need to abstract
    //this out completely into a position number everywhere...

    // convert position to cell id in x, y and z
    trex::TTPCCellInfo3D cell = fMasterLayout->GetPadPosID(hit->GetPosition(),hit->GetTime());

    // find minima and maxima
    minX = std::min(minX, cell.x);
    maxX = std::max(maxX, cell.x);
    minY = std::min(minY, cell.y);
    maxY = std::max(maxY, cell.y);
    minZ = std::min(minZ, cell.z);
    maxZ = std::max(maxZ, cell.z);
  };

  // determine if any hits have been added
  fHasHits = (maxX >= 0 && maxY >= 0 && maxZ >= 0);
  if (!fHasHits) return;

  // add minimum and maximum cell ids in x, y and z to layout
  fMasterLayout->SetRanges(minX,maxX, minY,maxY, minZ,maxZ);

  // loop over positions and charges and add a new pattern recognition cell for each one
  for (std::vector<trex::TTPCHitPad*>::iterator hitIt = hits.begin(); hitIt != hits.end(); ++hitIt){
    trex::TTPCHitPad* hit = *hitIt;

    // convert position to cell id in x, y and z
    trex::TTPCCellInfo3D cell = fMasterLayout->GetPadPosID(hit->GetPosition(), 0);

    // convert cell id in x, y and z to unique id
    long id = fMasterLayout->Mash(cell.x, cell.y, cell.z);

    // ignore cells below or above minima or maxima
    if (cell.x < minX || cell.y < minY || cell.z < minZ) continue;
    if (cell.x > maxX || cell.y > maxY || cell.z > maxZ) continue;

    // see if a cell already exists at this position
    std::map<long, trex::TTPCUnitVolume*>::iterator el = fMasterHitMap.find(id);
    // if cell doesn't already exist, define a new one at this position
    if(el == fMasterHitMap.end()){
      fMasterHitMap[id]=new trex::TTPCUnitVolume;
      trex::TTPCUnitVolume& curVol = *(fMasterHitMap[id]);

      curVol.SetCell(cell.x, cell.y, cell.z, cell.edgeX, cell.edgeY, cell.edgeZ, id);
      curVol.SetAux(cell.segX, cell.segY, cell.segZ);
      if(cell.segX > 0){
        curVol.SetTimeOffset(fMasterLayout->GetTPMin());
      }
      else{
        curVol.SetTimeOffset(fMasterLayout->GetTNMin());
      };

      // get MM information

      //MDH TODO - figure out if any of this is needed
      /*trex::TGeometryId geomId = hit->GetGeomId();
    unsigned int tpc = trex::TGeomInfo::Get().TPC().GeomIdToTPC(geomId);
    unsigned int half = trex::TGeomInfo::Get().TPC().GeomIdToHalf(geomId);
    unsigned int mm = trex::TGeomInfo::Get().TPC().GeomIdToMM(geomId);
    curVol.SetMMLoc(tpc, half, mm);
      curVol.SetFECASIC(fec, asic);
      curVol.SetRegion(asicRegionY, asicRegionZ);
      */

    };
    // increment charge and average position at this cell
    fMasterHitMap[id]->AddEvent(hit);
  };
}

//Output - reimplement
/*
void trex::TTPCTRExPatAlgorithm::GetPatterns(trex::TReconObjectContainer *foundPatterns){
  // add patterns from all sub events
  for(std::vector<trex::TTPCTRExPatSubAlgorithm*>::iterator subAlgIt = fSubAlgorithms.begin(); subAlgIt != fSubAlgorithms.end(); ++subAlgIt){
    trex::TTPCTRExPatSubAlgorithm* subAlg = *subAlgIt;
    trex::THandle<trex::TTPCPattern> foundPattern = subAlg->GetPattern();

    // ensure pattern exists and contains sensible numbers of paths and junctions
    if(!foundPattern) continue;

    int nPaths = foundPattern->GetNPaths();
    int nJunctions = foundPattern->GetNJunctions();

    bool errorInPattern = false;
    if(nPaths < 1) errorInPattern = true;
    if( (nPaths < 2 && nJunctions > 0) || (nPaths > 1 && nJunctions < 1) ) errorInPattern = true;
    if(errorInPattern){
      std::cout << " WARNING: pattern has " << nPaths << " paths and " << nJunctions << " junctions - something went wrong! " << std::endl;
    }
    else{
      foundPatterns->push_back(foundPattern);
    };
  };

  // print debug output
  VerifyHitsFull(foundPatterns);
  VerifyUsedUnusedFull(foundPatterns);
  }*/

//Top-level code - reimplement

void trex::TTPCTRExPatAlgorithm::Process(std::vector<trex::TTPCHitPad*>& hits, std::vector<trex::TTPCHitPad*>& used, std::vector<trex::TTPCHitPad*>& unused){

  // master layout for all sub-events
  fMasterLayout = new trex::TTPCLayout();
  fDriftVelocity = fMasterLayout->GetDriftSpeed();

  // reset group IDs
  trex::TTPCVolGroup::ResetFreeID();
  // prepare hits
  PrepareHits(hits);

  // master manager for all unit volumes
  fMasterVolGroupMan = new trex::TTPCVolGroupMan(fMasterLayout);
  fMasterVolGroupMan->AddPrimaryHits(fMasterHitMap);

  // split all hits up into lists of sub events, with separate group for high charge ones if needed
  std::vector< trex::TTPCVolGroup > subEvents;
  
  fMasterVolGroupMan->GetConnectedHits(subEvents, trex::TTPCConnection::path);
  
  // push all groups of decent size into sub events
  for(unsigned int i=0; i<subEvents.size(); ++i){
    trex::TTPCVolGroup& subEvent = subEvents.at(i);
    
    if(fMasterVolGroupMan->CheckUsability(subEvent)){
      // create sub-algorithm for each sub-event
      fSubAlgorithms.emplace_back(fMasterLayout);

      // set hit selection for sub-event
      fSubAlgorithms.back().SetUpHits(subEvent.GetHitMap());
    };
  };

  // first pass of processing
  int subEvent = 0;
  for(std::vector<trex::TTPCTRExPatSubAlgorithm>::iterator algIt = fSubAlgorithms.begin(); algIt != fSubAlgorithms.end(); ++algIt){
    trex::TTPCTRExPatSubAlgorithm alg = *algIt;
    subEvent++;
    alg.ProduceContainers();
  };

  // set up container for hitpad level unused
  std::vector<trex::TTPCHitPad*> usedTREx;

  // get patterns
  subEvent = 0;
  for(std::vector<trex::TTPCTRExPatSubAlgorithm>::iterator algIt = fSubAlgorithms.begin(); algIt != fSubAlgorithms.end(); ++algIt){
    trex::TTPCTRExPatSubAlgorithm& alg = *algIt;
    subEvent++;

    //MDH TODO - This is where the output needs to be generated...
    // produce pattern for each sub-algorithm
    //alg.ProducePattern(usedTREx);
  };

  // fill unused hits
  //FillUsedUnusedHits(usedTREx, used, unused);

  // clean up
  //delete usedTREx;

}
