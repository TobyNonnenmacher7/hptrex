// eddy
#include "TTPCTRExPatAlgorithm.hxx"

#include "TCanvas.h"
#include "TGraph.h"
#include "TH2F.h"

trex::TTPCTRExPatAlgorithm::TTPCTRExPatAlgorithm(TFile* plotFile) {
  // has no hits by default
  fHasHits = false;
  fPlotFile=plotFile;

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

  fHits.clear();
}

void trex::TTPCTRExPatAlgorithm::PrepareHits(std::vector<trex::TTPCHitPad*>& hits){
  fHits=hits;
  if(!fHits.size())return;
  // work out minimum and maximum cell ids in x, y and z
  int minX = +9999999;
  int maxX = -9999999;
  int minY = +9999999;
  int maxY = -9999999;
  int minZ = +9999999;
  int maxZ = -9999999;
  for (std::vector<trex::TTPCHitPad*>::iterator hitIt = hits.begin(); hitIt != hits.end(); ++hitIt){
    trex::TTPCHitPad* hit = *hitIt;

    // convert position to cell id in x, y and z
    trex::TTPCCellInfo3D cell = fMasterLayout->GetPadPosID(hit->GetPosition());

    // find minima and maxima
    minX = std::min(minX, cell.x);
    maxX = std::max(maxX, cell.x);
    minY = std::min(minY, cell.y);
    maxY = std::max(maxY, cell.y);
    minZ = std::min(minZ, cell.z);
    maxZ = std::max(maxZ, cell.z);
  };


  // add minimum and maximum cell ids in x, y and z to layout
  fMasterLayout->SetRanges(minX,maxX, minY,maxY, minZ,maxZ);

  // loop over positions and charges and add a new pattern recognition cell for each one
  for (std::vector<trex::TTPCHitPad*>::iterator hitIt = hits.begin(); hitIt != hits.end(); ++hitIt){
    trex::TTPCHitPad* hit = *hitIt;

    // convert position to cell id in x, y and z
    trex::TTPCCellInfo3D cell = fMasterLayout->GetPadPosID(hit->GetPosition());

    // convert cell id in x, y and z to unique id
    long id = fMasterLayout->Mash(cell.x, cell.y, cell.z);

    // ignore cells below or above minima or maxima
    if (cell.x < minX || cell.y < minY || cell.z < minZ){
      std::cout<<"Cell out of range!"<<std::endl;
      continue;
    }
    if (cell.x > maxX || cell.y > maxY || cell.z > maxZ){
 std::cout<<"Cell out of range!"<<std::endl;
      continue;
    }

    // see if a cell already exists at this position
    std::map<long, trex::TTPCUnitVolume*>::iterator el = fMasterHitMap.find(id);
    // if cell doesn't already exist, define a new one at this position
    if(el == fMasterHitMap.end()){
      fMasterHitMap[id]=new trex::TTPCUnitVolume;
      trex::TTPCUnitVolume& curVol = *(fMasterHitMap[id]);

      curVol.SetCell(cell.x, cell.y, cell.z, id);

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

  static unsigned int iEvt=0;

  std::cout<<"Number of hit pads: "<<hits.size()<<std::endl;
  // master layout for all sub-events
  fMasterLayout = new trex::TTPCLayout();
  // reset group IDs
  trex::TTPCVolGroup::ResetFreeID();
  // prepare hits
  PrepareHits(hits);
  // master manager for all unit volumes
  fMasterVolGroupMan = new trex::TTPCVolGroupMan(fMasterLayout);
  std::cout<<"Number of hits: "<<fMasterHitMap.size()<<std::endl;
  fMasterVolGroupMan->AddPrimaryHits(fMasterHitMap);

  // split all hits up into lists of sub events, with separate group for high charge ones if needed
  std::vector< trex::TTPCVolGroup > subEvents;
  fMasterVolGroupMan->GetConnectedHits(subEvents, trex::TTPCConnection::path);
  std::cout<<"Number of subevents: "<<subEvents.size()<<std::endl;
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
  for(std::vector<trex::TTPCTRExPatSubAlgorithm>::iterator algIt = fSubAlgorithms.begin(); algIt != fSubAlgorithms.end(); ++algIt){
    trex::TTPCTRExPatSubAlgorithm& alg = *algIt;
    alg.ProduceContainers();
    alg.ProducePattern();
  };


  // set up container for hitpad level unused
  std::vector<trex::TTPCHitPad*> usedTREx;

  std::vector<TGraph> xyGraphs;
  std::vector<TGraph> xzGraphs;

  // get patterns

  
  int iColor=0;
  int colors[11]={kBlue, kRed, kYellow, kGreen, kMagenta, kCyan, kOrange, kPink, kAzure, kSpring, kViolet};
  
  
  for(std::vector<trex::TTPCTRExPatSubAlgorithm>::iterator algIt = fSubAlgorithms.begin(); algIt != fSubAlgorithms.end(); ++algIt){
    trex::TTPCTRExPatSubAlgorithm& alg = *algIt;
    std::vector<std::vector<trex::TTPCHitPad*> >& subPaths= alg.GetPaths();
    std::vector<std::vector<trex::TTPCHitPad*> >& subJuncts=alg.GetJunctions();
    std::vector< std::vector<unsigned int> >& subJPMap=alg.GetJunctionsToPathsMap();

    for(auto iPath=subPaths.begin();iPath!=subPaths.end();++iPath){
      int color_index = iColor%11;
      int color_increment = iColor%4;
      xyGraphs.emplace_back(1);
      xzGraphs.emplace_back(1);
      xyGraphs.back().SetMarkerColor(colors[color_index]+color_increment);
      xyGraphs.back().SetMarkerStyle(20);
      xyGraphs.back().SetMarkerSize(0.5);
      xzGraphs.back().SetMarkerColor(colors[color_index]+color_increment);
      xzGraphs.back().SetMarkerStyle(20);
      xzGraphs.back().SetMarkerSize(0.5);
      iColor++;
      unsigned int iPt=0;
      for(auto iHit=iPath->begin();iHit!=iPath->end();++iHit){
	TVector3 pos=(*iHit)->GetPosition();
	xyGraphs.back().SetPoint(iPt,pos.X(),pos.Y());
	xzGraphs.back().SetPoint(iPt++,pos.X(),pos.Z());
	if(std::find(usedTREx.begin(),usedTREx.end(),*iHit)==usedTREx.end()){
	  usedTREx.push_back(*iHit);
	}
      }
    }
    
    for(auto iJunct=subJuncts.begin();iJunct!=subJuncts.end();++iJunct){
      int color_index = iColor%11;
      int color_increment = iColor%4;
      xyGraphs.emplace_back(1);
      xzGraphs.emplace_back(1);
      xyGraphs.back().SetMarkerColor(colors[color_index]+color_increment);
      xyGraphs.back().SetMarkerStyle(21);
      xyGraphs.back().SetMarkerSize(0.5);
      xzGraphs.back().SetMarkerColor(colors[color_index]+color_increment);
      xzGraphs.back().SetMarkerStyle(21);
      xzGraphs.back().SetMarkerSize(0.5);
      iColor++;
      unsigned int iPt=0;
      for(auto iHit=iJunct->begin();iHit!=iJunct->end();++iHit){
	TVector3 pos=(*iHit)->GetPosition();
	xyGraphs.back().SetPoint(iPt,pos.X(),pos.Y());
	xzGraphs.back().SetPoint(iPt++,pos.X(),pos.Z());
	if(std::find(usedTREx.begin(),usedTREx.end(),*iHit)==usedTREx.end()){
	  usedTREx.push_back(*iHit);
	}
	
      }
      
    }
  }


  if(hits.size()){
    fPlotFile->cd();

    std::vector<trex::TTPCHitPad*> unusedHits;  
    for(auto iHit=hits.begin();iHit!=hits.end();++iHit){
      if(std::find(usedTREx.begin(),usedTREx.end(),*iHit)==usedTREx.end()){
	unusedHits.push_back(*iHit);
      }
    }
    
    xyGraphs.emplace_back(1);
    xzGraphs.emplace_back(1);
    xyGraphs.back().SetMarkerColor(1);
    xyGraphs.back().SetMarkerStyle(20);
    xyGraphs.back().SetMarkerSize(0.2);
    xzGraphs.back().SetMarkerColor(1);
    xzGraphs.back().SetMarkerStyle(20);
    xzGraphs.back().SetMarkerSize(0.2);

    unsigned int iPt=0;
    
    for(auto iHit=unusedHits.begin();iHit!=unusedHits.end();++iHit){
      TVector3 pos=(*iHit)->GetPosition();
      xyGraphs.back().SetPoint(iPt,pos.X(),pos.Y());
      xzGraphs.back().SetPoint(iPt++,pos.X(),pos.Z());
    }
    
    char buf[20];
    sprintf(buf,"evt_%d_xy",iEvt);
    TCanvas cxy(buf,buf);
        
    TH2F dummyxy("XY-view","XY-view",
                 1000,fMasterLayout->GetMinPos().X()-10.,fMasterLayout->GetMaxPos().X()+10.,
                 1000,fMasterLayout->GetMinPos().Y()-10.,fMasterLayout->GetMaxPos().Y()+10.);
    
    dummyxy.Draw();
    
    for(auto iGr=xyGraphs.begin();iGr!=xyGraphs.end();++iGr){
      iGr->Draw("Psame");
    }

    sprintf(buf,"evt_%d_xy.pdf",iEvt);
    
    cxy.SaveAs(buf);
    cxy.Write();
    
    sprintf(buf,"evt_%d_xz",iEvt);
    TCanvas cxz(buf,buf);
    
    TH2F dummyxz("XZ-view","XZ-view",
		 1000,fMasterLayout->GetMinPos().X()-10.,fMasterLayout->GetMaxPos().X()+10.,
		 1000,fMasterLayout->GetMinPos().Z()-10.,fMasterLayout->GetMaxPos().Z()+10.);
    

    dummyxz.Draw();
    
    for(auto iGr=xzGraphs.begin();iGr!=xzGraphs.end();++iGr){
      iGr->Draw("Psame");
    }

    sprintf(buf,"evt_%d_xz.pdf",iEvt);    
    cxz.SaveAs(buf);
    
    cxz.Write();
  }
    
    //MDH TODO - This is where the output needs to be generated...

  // fill unused hits
  //FillUsedUnusedHits(usedTREx, used, unused);

  // clean up
  //delete usedTREx;

  ++iEvt;
}
