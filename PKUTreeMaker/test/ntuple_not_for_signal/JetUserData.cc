#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/DependentRecordImplementation.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/EDMException.h"

#include "DataFormats/PatCandidates/interface/Jet.h"
#include "DataFormats/PatCandidates/interface/Electron.h"
#include "DataFormats/PatCandidates/interface/Muon.h"

// Fastjet (for creating subjets)
#include <fastjet/JetDefinition.hh>
#include <fastjet/PseudoJet.hh>
#include "fastjet/tools/Filter.hh"
#include <fastjet/ClusterSequence.hh>
#include <fastjet/ClusterSequenceArea.hh>

// Vertex
#include "DataFormats/VertexReco/interface/Vertex.h"
#include "DataFormats/VertexReco/interface/VertexFwd.h"

// trigger
#include "HLTrigger/HLTcore/interface/HLTConfigProvider.h"
#include "DataFormats/Common/interface/TriggerResults.h"
#include "DataFormats/HLTReco/interface/TriggerEvent.h"
#include "DataFormats/HLTReco/interface/TriggerObject.h"
#include "DataFormats/HLTReco/interface/TriggerTypeDefs.h" // gives access to the (release cycle dependent) trigger object codes
#include "DataFormats/JetReco/interface/Jet.h"

// SVTagInfo
#include "DataFormats/BTauReco/interface/SecondaryVertexTagInfo.h"

// JEC/JER
#include "CondFormats/JetMETObjects/interface/JetCorrectorParameters.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "JetMETCorrections/Objects/interface/JetCorrectionsRecord.h"
#include "JetMETCorrections/Modules/interface/JetResolution.h"
#include <TRandom3.h>
#include "CondFormats/JetMETObjects/interface/FactorizedJetCorrector.h"
#include "CommonTools/Utils/interface/StringCutObjectSelector.h"

#include <TFile.h>
#include <TH1F.h>
#include <TGraphAsymmErrors.h>
#include <TLorentzVector.h>
#include <vector>

using namespace fastjet;
using namespace reco;
using namespace edm;
using namespace std;
using namespace trigger;

#define DEBUG false

typedef std::vector<pat::Jet> PatJetCollection;

class JetUserData : public edm::EDProducer {
	public:
		JetUserData( const edm::ParameterSet & );   
		virtual double get_JER_corr(float JERSF, bool isMC, pat::Jet jet, double conSize, float PtResolution, double jetCorrFactor);

	private:
		void produce( edm::Event &, const edm::EventSetup & );
		bool isMatchedWithTrigger(const pat::Jet&, trigger::TriggerObjectCollection,int&,double&,double);

		edm::EDGetTokenT<std::vector<pat::Jet> >     jetToken_;

		//InputTag jetLabel_;
		EDGetTokenT< std::vector< pat::Jet > > jLabel_;
		EDGetTokenT<double> rhoLabel_;
		double coneSize_;
		bool getJERFromTxt_;
		std::string jetCorrLabel;
		std::string jerLabel_;
		std::string resolutionsFile_;
		std::string scaleFactorsFile_;
		EDGetTokenT< edm::TriggerResults > triggerResultsLabel_;
		EDGetTokenT< trigger::TriggerEvent > triggerSummaryLabel_;
		InputTag hltJetFilterLabel_;
		std::string hltPath_;
		double hlt2reco_deltaRmax_;
		std::string candSVTagInfos_;
		HLTConfigProvider hltConfig;
		int triggerBit;
		TRandom3 rnd_;
		JME::JetParameters jetParam;
		JME::JetResolution resolution;
		JME::JetResolutionScaleFactor res_sf;
		//////// for JEC before JEC uncertainty
		FactorizedJetCorrector* jecOffset_;
		FactorizedJetCorrector* jecAK4_;
		std::vector<std::string> jetCorrLabel_;
		std::vector<std::string> jecAK4chsLabels_;
		std::vector<std::string> offsetCorrLabel_;
		edm::EDGetTokenT<reco::VertexCollection> VertexToken_;
		//////// Meng 2017/5/8
};


JetUserData::JetUserData(const edm::ParameterSet& iConfig) :
	jLabel_             (consumes<std::vector<pat::Jet>>(iConfig.getParameter<edm::InputTag>("jetLabel"))), 
	rhoLabel_           (consumes<double>(iConfig.getParameter<edm::InputTag>("rho"))),
	coneSize_           (iConfig.getParameter<double>("coneSize")),
	getJERFromTxt_      (iConfig.getParameter<bool>("getJERFromTxt")),
	jetCorrLabel       (iConfig.getParameter<std::string>("jetCorrLabel")),
	triggerResultsLabel_(consumes<edm::TriggerResults>(iConfig.getParameter<edm::InputTag>("triggerResults"))),
	triggerSummaryLabel_(consumes<trigger::TriggerEvent>(iConfig.getParameter<edm::InputTag>("triggerSummary"))),
	hltJetFilterLabel_  (iConfig.getParameter<edm::InputTag>("hltJetFilter")),   //trigger objects we want to match
	hltPath_            (iConfig.getParameter<std::string>("hltPath")),
	hlt2reco_deltaRmax_ (iConfig.getParameter<double>("hlt2reco_deltaRmax")),
	candSVTagInfos_         (iConfig.getParameter<std::string>("candSVTagInfos")),
	jecAK4chsLabels_    (iConfig.getParameter<std::vector<std::string>>("jecAK4chsPayloadNames_jetUserdata")),
	VertexToken_ (consumes<reco::VertexCollection> (iConfig.getParameter<edm::InputTag>( "vertex_jetUserdata" )))
{
	if (getJERFromTxt_) {
		resolutionsFile_  = iConfig.getParameter<std::string>("resolutionsFile");
		scaleFactorsFile_ = iConfig.getParameter<std::string>("scaleFactorsFile");
	} else
		jerLabel_         = iConfig.getParameter<std::string>("jerLabel");
	//////// for JEC before JEC uncertainty
	jetCorrLabel_ = jecAK4chsLabels_;
	offsetCorrLabel_.push_back(jetCorrLabel_[0]);

	std::vector<JetCorrectorParameters> vPar;
	for ( std::vector<std::string>::const_iterator payloadBegin = jecAK4chsLabels_.begin(), payloadEnd = jecAK4chsLabels_.end(), ipayload = payloadBegin; ipayload != payloadEnd; ++ipayload ) {
		JetCorrectorParameters pars(*ipayload);
		vPar.push_back(pars);
	}
	jecAK4_ = new FactorizedJetCorrector(vPar);
	vPar.clear();
	for ( std::vector<std::string>::const_iterator payloadBegin = offsetCorrLabel_.begin(), payloadEnd = offsetCorrLabel_.end(), ipayload = payloadBegin; ipayload != payloadEnd; ++ipayload ) {
		JetCorrectorParameters pars(*ipayload);
		vPar.push_back(pars);
	}
	jecOffset_ = new FactorizedJetCorrector(vPar);
	vPar.clear();
	//////// Meng 2017/5/8
	produces<vector<pat::Jet> >();
}

double JetUserData::get_JER_corr(float JERSF, bool isMC, pat::Jet jet, double conSize, float PtResolution, double jetCorrFactor){
	double JER_corrFactor = 1.;
	if(isMC) {
		bool isGenMatched = 0;
		const reco::GenJet* genJet=jet.genJet();
		reco::Candidate::LorentzVector rawJetP4 = jet.correctedP4(0);
		if (genJet) {
			TLorentzVector jetp4, genjetp4;
			jetp4.SetPtEtaPhiE(jetCorrFactor*rawJetP4.pt(), jet.eta(), jet.phi(), jetCorrFactor*rawJetP4.energy());
			genjetp4.SetPtEtaPhiE(genJet->pt(), genJet->eta(), genJet->phi(), genJet->energy());
			float dR = jetp4.DeltaR(genjetp4);
			float dPt = jetCorrFactor*rawJetP4.pt()-genJet->pt();
			if ((dR<conSize/2.0)&&(fabs(dPt)<(3*PtResolution*jetCorrFactor*rawJetP4.pt()))) {
				isGenMatched = 1;
				JER_corrFactor = std::max(0., 1 + (JERSF     - 1) * dPt / (jetCorrFactor*rawJetP4.pt()));
			}
			if (!isGenMatched && JERSF>1) {
				double sigma = std::sqrt(JERSF * JERSF - 1) * PtResolution;
				JER_corrFactor = 1 + rnd_.Gaus(0, sigma);
			}
		}
	}
	return JER_corrFactor;
}


void JetUserData::produce( edm::Event& iEvent, const edm::EventSetup& iSetup) {

	bool isMC = (!iEvent.isRealData());

	double jetCorrEtaMax           = 9.9;
	edm::Handle<reco::VertexCollection> vertices_;
	iEvent.getByToken(VertexToken_, vertices_);
	double nVtx = vertices_->size();

	edm::Handle<std::vector<pat::Jet> > jetHandle, packedjetHandle;
	iEvent.getByToken(jLabel_, jetHandle);
	auto_ptr<vector<pat::Jet> > jetColl( new vector<pat::Jet> (*jetHandle) );

	// JEC Uncertainty
	edm::ESHandle<JetCorrectorParametersCollection> JetCorrParColl;
	iSetup.get<JetCorrectionsRecord>().get(jetCorrLabel, JetCorrParColl); 
	JetCorrectorParameters const & JetCorrPar = (*JetCorrParColl)["Uncertainty"];
	JetCorrectionUncertainty jecUnc(JetCorrPar);

	// JER
	// Twiki: https://twiki.cern.ch/twiki/bin/view/CMSPublic/WorkBookJetEnergyResolution#Scale_factors
	// Recipe taken from: https://github.com/blinkseb/cmssw/blob/jer_fix_76x/JetMETCorrections/Modules/plugins/JetResolutionDemo.cc
	edm::Handle<double> rho;
	iEvent.getByToken(rhoLabel_, rho);
/*	JME::JetParameters jetParam;
	JME::JetResolution resolution;
	JME::JetResolutionScaleFactor res_sf;*/
	if (getJERFromTxt_) {
		resolution = JME::JetResolution(resolutionsFile_);
		res_sf = JME::JetResolutionScaleFactor(scaleFactorsFile_);
	} else {
		resolution = JME::JetResolution::get(iSetup, jerLabel_+"_pt");
		res_sf = JME::JetResolutionScaleFactor::get(iSetup, jerLabel_);
	}
	//---------for MET
	double skipEMfractionThreshold_ = 0.9;
	double type1JetPtThreshold_     = 10.0;
	bool skipEM_                    = true;
	bool skipMuons_                 = true;
	std::string skipMuonSelection_string = "isGlobalMuon | isStandAloneMuon";
        StringCutObjectSelector<reco::Candidate>* skipMuonSelection_ = new StringCutObjectSelector<reco::Candidate>(skipMuonSelection_string,true);
	//--------for MET
	for (size_t i = 0; i< jetColl->size(); i++){
		pat::Jet & jet = (*jetColl)[i];

		/////// for JEC before JEC uncertainty
		double jetCorrFactor = 1.;
		reco::Candidate::LorentzVector rawJetP4 = jet.correctedP4(0);
		reco::Candidate::LorentzVector rawJetP4_MET = jet.correctedP4(0);
		if ( fabs(rawJetP4.eta()) < jetCorrEtaMax ){
			jecAK4_->setJetEta( rawJetP4.eta() );
			jecAK4_->setJetPt ( rawJetP4.pt() );
			jecAK4_->setJetE  ( rawJetP4.energy() );
			jecAK4_->setJetPhi( rawJetP4.phi()    );
			jecAK4_->setJetA  ( jet.jetArea() );
			jecAK4_->setRho   ( *(rho.product()) );
			jecAK4_->setNPV   ( nVtx );
			jetCorrFactor = jecAK4_->getCorrection();
		}

		double jetCorrFactor_l1 = 1.;
		if ( fabs(rawJetP4.eta()) < jetCorrEtaMax ){
			jecOffset_->setJetEta( rawJetP4.eta()     );
			jecOffset_->setJetPt ( rawJetP4.pt()      );
			jecOffset_->setJetE  ( rawJetP4.energy()  );
			jecOffset_->setJetPhi( rawJetP4.phi()     );
			jecOffset_->setJetA  ( jet.jetArea()      );
			jecOffset_->setRho   ( *(rho.product())  );
			jecOffset_->setNPV   ( nVtx  );
			jetCorrFactor_l1 = jecOffset_->getCorrection();
		}


		// JEC uncertainty
		jecUnc.setJetPt (jetCorrFactor*rawJetP4.pt());// here you must use the CORRECTED jet pt
		jecUnc.setJetEta(jet.eta());
		double jecUncertainty_up = jecUnc.getUncertainty(true);//true = UP, false = DOWN //Meng Lu

		jecUnc.setJetPt (jetCorrFactor*rawJetP4.pt());// here you must use the CORRECTED jet pt
		jecUnc.setJetEta(jet.eta());
		double jecUncertainty_down = jecUnc.getUncertainty(false);//true = UP, false = DOWN //Meng Lu

		// JEC l1 uncertainty
		jecUnc.setJetPt (jetCorrFactor_l1*rawJetP4.pt());// here you must use the CORRECTED jet pt
		jecUnc.setJetEta(jet.eta());
                double jecUncertainty_l1_up = jecUnc.getUncertainty(true);//true = UP, false = DOWN //Meng Lu

		jecUnc.setJetPt (jetCorrFactor_l1*rawJetP4.pt());// here you must use the CORRECTED jet pt
                jecUnc.setJetEta(jet.eta());
                double jecUncertainty_l1_down = jecUnc.getUncertainty(false);//true = UP, false = DOWN //Meng Lu

 //-----------------------for MET
		double emEnergyFraction = jet.chargedEmEnergyFraction() + jet.neutralEmEnergyFraction();
                double corrEx_MET_JEC = 0;
                double corrEy_MET_JEC = 0;
                double corrSumEt_MET_JEC = 0;
		double corrEx_MET_JEC_up = 0;
                double corrEy_MET_JEC_up = 0;
                double corrSumEt_MET_JEC_up = 0;
		double corrEx_MET_JEC_down = 0;
                double corrEy_MET_JEC_down = 0;
                double corrSumEt_MET_JEC_down = 0;
                if ( !(skipEM_ && emEnergyFraction > skipEMfractionThreshold_) ){
                        if ( skipMuons_ ) {
                        const std::vector<reco::CandidatePtr> & cands = jet.daughterPtrVector();
                        for ( std::vector<reco::CandidatePtr>::const_iterator cand = cands.begin();
                                        cand != cands.end(); ++cand ) {
                                const reco::PFCandidate *pfcand = dynamic_cast<const reco::PFCandidate *>(cand->get());
                                const reco::Candidate *mu = (pfcand != 0 ? ( pfcand->muonRef().isNonnull() ? pfcand->muonRef().get() : 0) : cand->get());
                                if ( mu != 0 && (*skipMuonSelection_)(*mu) ) {
                                        reco::Candidate::LorentzVector muonP4 = (*cand)->p4();
                                        rawJetP4_MET -= muonP4;
                                }
                        }
                        }
                        reco::Candidate::LorentzVector corrJetP4_MET = jetCorrFactor*rawJetP4_MET;
			reco::Candidate::LorentzVector corrJetP4_up_MET = jetCorrFactor*(1+jecUncertainty_up)*rawJetP4_MET;
			reco::Candidate::LorentzVector corrJetP4_down_MET = jetCorrFactor*(1-jecUncertainty_down)*rawJetP4_MET;
                        reco::Candidate::LorentzVector rawJetP4offsetCorr_MET = jetCorrFactor_l1*rawJetP4_MET;
			reco::Candidate::LorentzVector rawJetP4offsetCorr_up_MET = jetCorrFactor_l1*(1+jecUncertainty_l1_up)*rawJetP4_MET;
			reco::Candidate::LorentzVector rawJetP4offsetCorr_down_MET = jetCorrFactor_l1*(1-jecUncertainty_l1_down)*rawJetP4_MET;
                        if ( corrJetP4_MET.pt() > type1JetPtThreshold_ ) {
                        corrEx_MET_JEC    -= (corrJetP4_MET.px() - rawJetP4offsetCorr_MET.px());
                        corrEy_MET_JEC    -= (corrJetP4_MET.py() - rawJetP4offsetCorr_MET.py());
                        corrSumEt_MET_JEC += (corrJetP4_MET.Et() - rawJetP4offsetCorr_MET.Et());
			corrEx_MET_JEC_up    -= (corrJetP4_up_MET.px() - rawJetP4offsetCorr_up_MET.px());
                        corrEy_MET_JEC_up    -= (corrJetP4_up_MET.py() - rawJetP4offsetCorr_up_MET.py());
                        corrSumEt_MET_JEC_up += (corrJetP4_up_MET.Et() - rawJetP4offsetCorr_up_MET.Et());
			corrEx_MET_JEC_down    -= (corrJetP4_down_MET.px() - rawJetP4offsetCorr_down_MET.px());
                        corrEy_MET_JEC_down    -= (corrJetP4_down_MET.py() - rawJetP4offsetCorr_down_MET.py());
                        corrSumEt_MET_JEC_down += (corrJetP4_down_MET.Et() - rawJetP4offsetCorr_down_MET.Et());
                        }
                }
//-----------------------for MET

		// JER
		jetParam.setJetPt(jetCorrFactor*rawJetP4.pt()).setJetEta(jet.eta()).setRho(*rho);// resolution depend on pt and eta, SF depend on eta and rho, so the parameter should be initialized with three parameters
		float PtResolution = resolution.getResolution(jetParam);
		float JERSF        = res_sf.getScaleFactor(jetParam);
		float JERSFUp      = res_sf.getScaleFactor(jetParam, Variation::UP);
		float JERSFDown    = res_sf.getScaleFactor(jetParam, Variation::DOWN);
		double corrEx_MET_JER = 0;
		double corrEx_MET_JER_up = 0;
		double corrEx_MET_JER_down = 0;
		double corrEy_MET_JER = 0;
                double corrEy_MET_JER_up = 0;
                double corrEy_MET_JER_down = 0;
		double corrSumEt_MET_JER = 0;
                double corrSumEt_MET_JER_up = 0;
                double corrSumEt_MET_JER_down = 0;

		// Hybrid scaling and smearing procedure applied:
		//   https://twiki.cern.ch/twiki/bin/viewauth/CMS/JetResolution#Smearing_procedures
		reco::Candidate::LorentzVector smearedP4_raw =jetCorrFactor*rawJetP4;
		reco::Candidate::LorentzVector smearedP4_up_raw =jetCorrFactor*rawJetP4;
		reco::Candidate::LorentzVector smearedP4_down_raw =jetCorrFactor*rawJetP4;

		reco::Candidate::LorentzVector smearedP4 =jetCorrFactor*rawJetP4;
                reco::Candidate::LorentzVector smearedP4_up =jetCorrFactor*rawJetP4;
                reco::Candidate::LorentzVector smearedP4_down =jetCorrFactor*rawJetP4;

		double aaa = get_JER_corr(JERSF, isMC, jet, coneSize_, PtResolution, jetCorrFactor);
		double bbb=1;
		double corrEx_MET_JER_bbb = 0; 
		reco::Candidate::LorentzVector smearedP4_tmp = aaa*smearedP4;
		corrEx_MET_JER_bbb -= (smearedP4_tmp.px() - smearedP4_raw.px());
		if(isMC) {
			// Hybrid method: Scale jet four momentum for well matched jets ...
			bool isGenMatched = 0;
			const reco::GenJet* genJet=jet.genJet();
			if (genJet) {
				TLorentzVector jetp4, genjetp4;
				jetp4.SetPtEtaPhiE(jetCorrFactor*rawJetP4.pt(), jet.eta(), jet.phi(), jetCorrFactor*rawJetP4.energy());
				genjetp4.SetPtEtaPhiE(genJet->pt(), genJet->eta(), genJet->phi(), genJet->energy());
				float dR = jetp4.DeltaR(genjetp4);
				float dPt = jetCorrFactor*rawJetP4.pt()-genJet->pt();
				if ((dR<coneSize_/2.0)&&(fabs(dPt)<(3*PtResolution*jetCorrFactor*rawJetP4.pt()))) {
					isGenMatched = 1;
					smearedP4     *= std::max(0., 1 + (JERSF     - 1) * dPt / (jetCorrFactor*rawJetP4.pt()));
					bbb = std::max(0., 1 + (JERSF     - 1) * dPt / (jetCorrFactor*rawJetP4.pt()));
					smearedP4_up     *= std::max(0., 1 + (JERSFUp     - 1) * dPt / (jetCorrFactor*rawJetP4.pt()));
					smearedP4_down     *= std::max(0., 1 + (JERSFDown     - 1) * dPt / (jetCorrFactor*rawJetP4.pt()));
				}
				// ... and gaussian smear the rest
				if (!isGenMatched && JERSF>1) {
					double sigma = std::sqrt(JERSF * JERSF - 1) * PtResolution;
					smearedP4 *= 1 + rnd_.Gaus(0, sigma);
					bbb= 1 + rnd_.Gaus(0, sigma);
				}
				if (!isGenMatched && JERSFUp>1) {
					double sigma_up = std::sqrt(JERSFUp * JERSFUp - 1) * PtResolution;
					smearedP4_up *= 1 + rnd_.Gaus(0, sigma_up);
				}
				if (!isGenMatched && JERSFDown>1) {
					double sigma_down = std::sqrt(JERSFDown * JERSFDown - 1) * PtResolution;
					smearedP4_down *= 1 + rnd_.Gaus(0, sigma_down);
				}
			}

			corrEx_MET_JER -= (smearedP4.px() - smearedP4_raw.px());
			corrEx_MET_JER_up -= (smearedP4_up.px() - smearedP4_up_raw.px());
			corrEx_MET_JER_down -= (smearedP4_down.px() - smearedP4_down_raw.px());
			corrEy_MET_JER -= (smearedP4.py() - smearedP4_raw.py());
                        corrEy_MET_JER_up -= (smearedP4_up.py() - smearedP4_up_raw.py());
                        corrEy_MET_JER_down -= (smearedP4_down.py() - smearedP4_down_raw.py());
			corrSumEt_MET_JER += (smearedP4.Et() - smearedP4_raw.Et());
                        corrSumEt_MET_JER_up += (smearedP4_up.Et() - smearedP4_up_raw.Et());
                        corrSumEt_MET_JER_down += (smearedP4_down.Et() - smearedP4_down_raw.Et());
		}

		jet.addUserFloat("jecUncertainty_up",   jecUncertainty_up);
		jet.addUserFloat("jecUncertainty_down",   jecUncertainty_down);
		jet.addUserFloat("jecUncertainty_l1_up", jecUncertainty_l1_up);
		jet.addUserFloat("jecUncertainty_l1_down",   jecUncertainty_l1_down);
		jet.addUserFloat("jetCorrFactor",jetCorrFactor);
		jet.addUserFloat("jetCorrFactor_l1",jetCorrFactor_l1);

		jet.addUserFloat("PtResolution", PtResolution);
		jet.addUserFloat("JERSF",        JERSF);
		jet.addUserFloat("JERSFUp",      JERSFUp);
		jet.addUserFloat("JERSFDown",    JERSFDown);
		jet.addUserFloat("SmearedPt",    smearedP4.pt());
		jet.addUserFloat("SmearedE",     smearedP4.energy());
		jet.addUserFloat("SmearedPt_up",    smearedP4_up.pt());
                jet.addUserFloat("SmearedE_up",     smearedP4_up.energy());
		jet.addUserFloat("SmearedPt_down",    smearedP4_down.pt());
                jet.addUserFloat("SmearedE_down",     smearedP4_down.energy());

		jet.addUserFloat("corrEx_MET_JEC",     corrEx_MET_JEC);
		jet.addUserFloat("corrEy_MET_JEC",     corrEy_MET_JEC);
		jet.addUserFloat("corrSumEt_MET_JEC",     corrSumEt_MET_JEC);
		jet.addUserFloat("corrEx_MET_JEC_up",     corrEx_MET_JEC_up);
                jet.addUserFloat("corrEy_MET_JEC_up",     corrEy_MET_JEC_up);
                jet.addUserFloat("corrSumEt_MET_JEC_up",     corrSumEt_MET_JEC_up);
		jet.addUserFloat("corrEx_MET_JEC_down",     corrEx_MET_JEC_down);
                jet.addUserFloat("corrEy_MET_JEC_down",     corrEy_MET_JEC_down);
                jet.addUserFloat("corrSumEt_MET_JEC_down",     corrSumEt_MET_JEC_down);

		jet.addUserFloat("corrEx_MET_JER", corrEx_MET_JER);
		jet.addUserFloat("corrEx_MET_JER_bbb", corrEx_MET_JER_bbb);
		jet.addUserFloat("aaa",aaa);
		jet.addUserFloat("bbb",bbb);
		jet.addUserFloat("corrEx_MET_JER_up", corrEx_MET_JER_up);
		jet.addUserFloat("corrEx_MET_JER_down", corrEx_MET_JER_down);
		jet.addUserFloat("corrEy_MET_JER", corrEy_MET_JER);
                jet.addUserFloat("corrEy_MET_JER_up", corrEy_MET_JER_up);
                jet.addUserFloat("corrEy_MET_JER_down", corrEy_MET_JER_down);
		jet.addUserFloat("corrSumEt_MET_JER", corrSumEt_MET_JER);
                jet.addUserFloat("corrSumEt_MET_JER_up", corrSumEt_MET_JER_up);
                jet.addUserFloat("corrSumEt_MET_JER_down", corrSumEt_MET_JER_down);
		unsigned int nSV(0);
		float SV0mass(-999), SV1mass(-999) ;

		std::vector<std::string>tagInfoLabels = jet.tagInfoLabels() ;
#if DEBUG
		bool hasCandSVTagInfo(jet.hasTagInfo(candSVTagInfos_)) ; 
		std::cout << " jetTagInfoLabels size = " << tagInfoLabels.size() << std::endl ; 
		for (std::string tagInfoLabel : tagInfoLabels ) {
			std::cout << ">>>> Jet has " << tagInfoLabel << std::endl ; 
		}
		std::cout << ">>>>>> candSVTagInfo label is " << candSVTagInfos_ << std::endl ; 
		std::cout << " hasCandSVTagInfo = " << hasCandSVTagInfo << std::endl ; 
#endif 

		if ( jet.hasTagInfo(candSVTagInfos_) ) {
			const reco::CandSecondaryVertexTagInfo *candSVTagInfo = jet.tagInfoCandSecondaryVertex("pfInclusiveSecondaryVertexFinder");
#if DEBUG
			if ( candSVTagInfo == nullptr ) std::cout << ">>>>>> candSVTagInfo ptr does not exist\n" ;
			else std::cout << ">>>>>> candSVTagInfo ptr exists\n" ;
#endif 
			nSV = candSVTagInfo->nVertices() ; 
			SV0mass = nSV > 0 ? ((candSVTagInfo->secondaryVertex(0)).p4()).mass() : -999 ;
			SV1mass = nSV > 1 ? ((candSVTagInfo->secondaryVertex(1)).p4()).mass() : -999 ;
			if ( nSV > 0 ) {
#if DEBUG
				std::cout << ">>>>> nSV = " << nSV 
					<< " SV0 mass = " << SV0mass 
					<< " SV1 mass = " << SV1mass 
					<< std::endl ; 
#endif 
			}
		}

		jet.addUserInt("nSV"     , nSV     ); 
		jet.addUserFloat("SV0mass", SV0mass); 
		jet.addUserFloat("SV1mass", SV1mass); 

		//// Jet constituent indices for lepton matching
		std::vector<unsigned int> constituentIndices;
		auto constituents = jet.daughterPtrVector();
		for ( auto & constituent : constituents ) {
			constituentIndices.push_back( constituent.key() );
		}

		jet.addUserData("pfKeys", constituentIndices );


	} //// Loop over all jets 

	iEvent.put( jetColl );

}

// ------------ method called once each job just after ending the event loop  ------------
	bool
JetUserData::isMatchedWithTrigger(const pat::Jet& p, trigger::TriggerObjectCollection triggerObjects, int& index, double& deltaR, double deltaRmax = 0.2)
{
	for (size_t i = 0 ; i < triggerObjects.size() ; i++){
		float dR = sqrt(pow(triggerObjects[i].eta()-p.eta(),2)+ pow(acos(cos(triggerObjects[i].phi()-p.phi())),2)) ;
		if (dR<deltaRmax) {
			deltaR = dR;
			index  = i;
			return true;
		}
	}
	return false;
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(JetUserData);