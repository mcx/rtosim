//#include "vosl/SharedData.h"
#include "rtosim/MarkersFromTrc.h"
#include "rtosim/EndOfData.h"
#include "rtosim/Mapper.h"
#include "rtosim/ArrayConverter.h"

#include <OpenSim/OpenSim.h>
#include <chrono>
#include <thread>
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <iostream>
using std::cout;
using std::endl;

namespace rtosim {

    MarkersFromTrc::MarkersFromTrc(
        MarkerSetQueue& outputMarkerSetQueue,
        rtosim::Concurrency::Latch& doneWithSubscriptions,
        rtosim::Concurrency::Latch& doneWithExecution,
        const std::string& osimModelFilename,
        const std::string& trcFilename,
        bool loop) :
        outputMarkerSetQueue_(outputMarkerSetQueue),
        doneWithSubscriptions_(doneWithSubscriptions),
        doneWithExecution_(doneWithExecution),
        trcFilename_(trcFilename),
        model_(osimModelFilename),
        loop_(loop),
        speedFactor_(1),
        framesToSkip_(0),
        noMarkers_(0){

        model_.initSystem();

        OpenSim::Array<std::string> markerNamesArray;
        std::vector<std::string> markerNamesFromModel;
        const_cast<OpenSim::MarkerSet&>(model_.getMarkerSet()).getMarkerNames(markerNamesArray);
        rtosim::ArrayConverter::toStdVector(markerNamesArray, markerNamesFromModel);

        OpenSim::MarkerData trcFile(trcFilename_);
        sampleFrequency_ = static_cast<unsigned>(trcFile.getCameraRate());
        trcFile.convertToUnits(model_.getLengthUnits());
        std::vector<std::string> markerNamesFromFile;
        rtosim::ArrayConverter::toStdVector(trcFile.getMarkerNames(), markerNamesFromFile);

        rtosim::Mapper markersMapper(markerNamesFromFile, markerNamesFromModel);

        for (size_t i(0); i < static_cast<size_t>(trcFile.getNumFrames()); ++i) {
            auto markersFromTrcArr(trcFile.getFrame(i).getMarkers());
            MarkerSetData markersFromTrc;
            for (unsigned j(0); j < markersFromTrcArr.size(); ++j)
                markersFromTrc.push_back(markersFromTrcArr[j]);

            MarkerSetFrame currentFrame;
            currentFrame.time = trcFile.getFrame(i).getFrameTime();
            currentFrame.data = markersMapper.map(markersFromTrc);
            frames_.push_back(currentFrame);
        }
    }

    void MarkersFromTrc::setOutputFrequency(double frequency) {

        speedFactor_ = frequency / sampleFrequency_;
    }

    void MarkersFromTrc::setSpeedFactor(double speedFactor) {

        speedFactor_ = speedFactor;
    }

    void MarkersFromTrc::setFramesToSkip(unsigned n) {

        framesToSkip_ = n;
    }

    void MarkersFromTrc::operator()(){

        if (loop_) runLoop();
        else run();
    }

    unsigned MarkersFromTrc::getSleepTime() const {

        return static_cast<unsigned>(std::floor(1000. / (sampleFrequency_*speedFactor_)));
    }

    void MarkersFromTrc::runLoop() {

        unsigned sleepTimeMilliseconds(getSleepTime());
        unsigned skipped(0);
        unsigned count(0);

        size_t noFrames(frames_.size());
        if (noFrames > 0)
            noMarkers_ = frames_.front().data.size();
        doneWithSubscriptions_.wait();

        //todo, fix shared variables
        while (1) {

            //        while (Shared::flowControl.getRunCondition()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMilliseconds));
            if (skipped == framesToSkip_) {
                auto frameToPush(frames_.at(count % noFrames));
                outputMarkerSetQueue_.push(frameToPush);
                skipped = 0;
            }
            else
                ++skipped;
            ++count;
        }
        sendEndOfData();
        doneWithExecution_.wait();
    }

    void MarkersFromTrc::run() {

        unsigned sleepTimeMilliseconds(getSleepTime());
        unsigned skipped(0);
        size_t noFrames(frames_.size());
        if (noFrames > 0)
            noMarkers_ = frames_.front().data.size();
        doneWithSubscriptions_.wait();

        for (auto& frame : frames_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMilliseconds));
            if (skipped == framesToSkip_) {
                outputMarkerSetQueue_.push(frame);
                skipped = 0;
            }
            else
                ++skipped;
        }
        sendEndOfData();
        doneWithExecution_.wait();
    }

    void MarkersFromTrc::sendEndOfData() {

        auto eodFrame(rtosim::EndOfData::get<MarkerSetFrame>());
        eodFrame.data.resize(noMarkers_);
        outputMarkerSetQueue_.push(eodFrame);
#ifdef RTOSIM_DEBUG
        cout << "MarkersFromTrc waiting\n";
#endif
    }

    MarkersFromTrc::~MarkersFromTrc() {
#ifdef RTOSIM_DEBUG
        cout << "Closing MarkersProducer..." << endl;
#endif
}
}