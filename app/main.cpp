#include "runtime/runtime.h"
#include "runtime/native_window.h"
#include "runtime/native_local_players.h"
#include "runtime/native_controllers.h"
#include "runtime/native_input_recording.h"
#include "runtime/audit_stage.h"
#include "stack_sampler.h"
#include <cstring>
#include <cstdio>
#include <exception>
#include <optional>
#include <array>
#include <charconv>

namespace {
struct Options {
    std::filesystem::path image,store,content,captures,controllerInput,frameTiming,inputRecording,inputPlayback,resourceAudit;
    std::string auditStage;
    std::optional<std::string> create;
    std::array<std::optional<std::string>,4> profiles;
    bool hold=false,list=false,captureOnRequest=false,frameTimingFramesOnly=false,renderTestFirstMission=false;
    bool firstMissionCompletion=false;
    bool bartmanBegins=false;
    bool auditStagePlayIntro=false;
    bool vsync=false;
    bool autoDefeatLocEnemies=false;
    bool inputRecordingAutoStart=false,inputPlaybackFromFirstPoll=false,inputPlaybackContinueLive=false;
    bool uncappedFrameRate=false;
    uint32_t frameRate=0;
    uint64_t inputPlaybackStartScene=0,inputPlaybackExpectedEndScene=0;
};
Options parse(int argc,char** argv) {
    Options out;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        auto value=[&]() -> std::string {
            if(i+1>=argc || !argv[i+1][0]) throw std::runtime_error("Missing value for "+arg);
            return argv[++i];
        };
        if(arg=="--image" && out.image.empty()) out.image=value();
        else if(arg=="--profile-store" && out.store.empty()) out.store=value();
        else if(arg=="--content-store" && out.content.empty()) out.content=value();
        else if(arg=="--create-local-profile" && !out.create) out.create=value();
        else if(arg=="--list-local-profiles" && !out.list) out.list=true;
        else if(arg=="--hold-on-failure" && !out.hold) out.hold=true;
        else if(arg=="--capture-frames" && out.captures.empty()) out.captures=value();
        else if(arg=="--capture-on-request" && !out.captureOnRequest) out.captureOnRequest=true;
        else if(arg=="--frame-rate" && !out.frameRate){const auto text=value();uint32_t rate{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),rate);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!rate||!Simpsons::NativeVideoSettings::validFrameRate(rate))throw std::runtime_error("Frame rate must be 30, 60, 90, 120, 144, 165 or 240");out.frameRate=rate;}
        else if(arg=="--uncapped-frame-rate" && !out.uncappedFrameRate) out.uncappedFrameRate=true;
        else if(arg=="--vsync" && !out.vsync) out.vsync=true;
        else if(arg=="--frame-timing" && out.frameTiming.empty()) out.frameTiming=value();
        else if(arg=="--frame-timing-frames-only" && !out.frameTimingFramesOnly) out.frameTimingFramesOnly=true;
        else if(arg=="--controller-input" && out.controllerInput.empty()) out.controllerInput=value();
        else if(arg=="--input-recording-directory" && out.inputRecording.empty()) out.inputRecording=value();
        else if(arg=="--input-recording-auto-start" && !out.inputRecordingAutoStart) out.inputRecordingAutoStart=true;
        else if(arg=="--input-playback" && out.inputPlayback.empty()) out.inputPlayback=value();
        else if(arg=="--input-playback-from-first-poll" && !out.inputPlaybackFromFirstPoll) out.inputPlaybackFromFirstPoll=true;
        else if(arg=="--input-playback-continue-live" && !out.inputPlaybackContinueLive) out.inputPlaybackContinueLive=true;
        else if(arg=="--input-playback-start-scene" && !out.inputPlaybackStartScene) {
            const auto digits=value();
            const auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),out.inputPlaybackStartScene);
            if(parsed.ec!=std::errc{} || parsed.ptr!=digits.data()+digits.size() || !out.inputPlaybackStartScene)
                throw std::runtime_error("Input playback start scene must be a positive integer");
        }
        else if(arg=="--input-playback-expected-end-scene" && !out.inputPlaybackExpectedEndScene) {
            const auto digits=value();
            const auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),out.inputPlaybackExpectedEndScene);
            if(parsed.ec!=std::errc{} || parsed.ptr!=digits.data()+digits.size() || !out.inputPlaybackExpectedEndScene)
                throw std::runtime_error("Input playback expected end scene must be a positive integer");
        }
        else if(arg=="--render-test-first-mission" && !out.renderTestFirstMission) out.renderTestFirstMission=true;
        else if(arg=="--first-mission-completion" && !out.firstMissionCompletion) out.firstMissionCompletion=true;
        else if(arg=="--bartman-begins" && !out.bartmanBegins) out.bartmanBegins=true;
        else if(arg=="--stage" && out.auditStage.empty()) {
            out.auditStage=value();
            if(!Simpsons::isAuditStage(out.auditStage))throw std::runtime_error("Unknown packaged audit stage");
        }
        else if(arg=="--resource-audit" && out.resourceAudit.empty()) out.resourceAudit=value();
        else if(arg=="--play-stage-intro" && !out.auditStagePlayIntro) out.auditStagePlayIntro=true;
        else if(arg=="--auto-defeat-loc-enemies" && !out.autoDefeatLocEnemies) out.autoDefeatLocEnemies=true;
        else if(arg=="--local-profile") {
            const auto selection=value();
            if(selection.size()<3 || selection[0]<'0' || selection[0]>'3' || selection[1]!=':')
                throw std::runtime_error("Local profile selection must be SLOT:ID with slot 0..3");
            auto& slot=out.profiles[size_t(selection[0]-'0')];
            if(slot) throw std::runtime_error("Duplicate local profile slot");
            slot=selection.substr(2);
        } else throw std::runtime_error("Unknown or repeated option: "+arg);
    }
    const bool management=out.create.has_value() || out.list;
    if(unsigned(out.firstMissionCompletion)+unsigned(out.renderTestFirstMission)+unsigned(out.bartmanBegins)+unsigned(!out.auditStage.empty())>1)
        throw std::runtime_error("Choose one direct stage or mission-complete transition");
    if(out.auditStagePlayIntro&&out.auditStage.empty())throw std::runtime_error("Stage intro playback requires --stage");
    if(out.captureOnRequest && out.captures.empty())throw std::runtime_error("On-request capture requires --capture-frames");
    if(out.frameTimingFramesOnly && out.frameTiming.empty())throw std::runtime_error("Frame-only timing requires --frame-timing");
    if(out.inputRecordingAutoStart && (out.inputRecording.empty() || !out.renderTestFirstMission))
        throw std::runtime_error("Automatic input recording requires a recording directory and direct first-mission launch");
    if(out.inputPlaybackFromFirstPoll && out.inputPlaybackStartScene)
        throw std::runtime_error("Choose either first-poll playback or a playback start scene");
    if(out.inputPlayback.empty()==(out.inputPlaybackFromFirstPoll || out.inputPlaybackStartScene) ||
       out.inputPlaybackContinueLive!=bool(out.inputPlaybackExpectedEndScene) ||
       (out.inputPlaybackContinueLive && !out.renderTestFirstMission) ||
       (!out.inputPlayback.empty() && (!out.controllerInput.empty() ||
          (!out.inputRecording.empty() && (!out.inputPlaybackContinueLive || !out.inputRecordingAutoStart)))))
        throw std::runtime_error("Input playback requires one start gate, an exclusive input source and a checkpoint scene for live handoff");
    bool anyProfile=false;
    for(const auto& slot:out.profiles) anyProfile|=slot.has_value();
    if(management) {
        if(out.renderTestFirstMission)throw std::runtime_error("First-mission rendering test is a game launch option");
        if(out.firstMissionCompletion)throw std::runtime_error("First-mission completion is a game launch option");
        if(out.bartmanBegins)throw std::runtime_error("Bartman Begins is a game launch option");
        if(!out.auditStage.empty()||!out.resourceAudit.empty())throw std::runtime_error("Resource audit and stage selection are game launch options");
        if(out.autoDefeatLocEnemies)throw std::runtime_error("Automatic Land of Chocolate enemy defeat is a game launch option");
        if(!out.inputRecording.empty() || !out.inputPlayback.empty())throw std::runtime_error("Input recording/playback is a game launch option");
        if(out.frameRate)throw std::runtime_error("Frame rate is a game launch option");
        if(out.uncappedFrameRate)throw std::runtime_error("Uncapped frame rate is a game launch option");
        if(out.vsync)throw std::runtime_error("Vsync is a game launch option");
        if((out.create && out.list) || out.store.empty() || !out.image.empty() || !out.content.empty() || !out.captures.empty() || !out.controllerInput.empty() || !out.frameTiming.empty() || out.hold || anyProfile)
            throw std::runtime_error("Profile management requires --profile-store and exactly one create/list operation");
    } else if(out.image.empty()) throw std::runtime_error("A game launch requires --image");
    if(out.uncappedFrameRate && out.frameRate) throw std::runtime_error("Choose either --frame-rate or --uncapped-frame-rate");
    return out;
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    // Batch per-frame diagnostic writes (NATIVE PRESENT, depth copies,
    // billboards, frame-timing summaries). The game logs several lines per
    // presentation to stderr, which is unbuffered by default; each write would
    // otherwise trap to the kernel and the log file. A 1 MiB full buffer keeps
    // every byte while collapsing hundreds of writes per second into occasional
    // flushes. Explicit flushes on termination/failure preserve crash evidence;
    // FrameTiming already flushes its CSV every 120 presentations.
    static char stderrBuffer[1<<20];
    setvbuf(stderr, stderrBuffer, _IOFBF, sizeof(stderrBuffer));
    Options options;
    try {options=parse(argc,argv);}
    catch(const std::exception& e) {
        fprintf(stderr,"%s\nUsage: SimpsonsNative --image <analysis/simpsons.pe> [--hold-on-failure] [--profile-store <directory>] [--local-profile <SLOT:ID>] [--content-store <directory>] [--capture-frames <directory>] [--capture-on-request] [--frame-timing <csv>] [--frame-timing-frames-only] [--controller-input <existing-file>]\n"
            "       Add --input-recording-directory <directory> [--input-recording-auto-start] for F8/F9 input recording.\n"
            "       Add --input-playback <inputs.jsonl> with --input-playback-start-scene <count> or --input-playback-from-first-poll.\n"
            "       Add --input-playback-continue-live --input-playback-expected-end-scene <count> for input replay handoff.\n"
            "       Vsync is off by default; add --vsync for DXGI Present(1,0).\n"
            "       --frame-rate supports 30, 60, 90, 120 (default), 144, 165 or 240; --uncapped-frame-rate removes the cap.\n"
            "       Add --render-test-first-mission for a temporary direct Land of Chocolate rendering test.\n"
            "       Add --first-mission-completion to open the original Land of Chocolate completion transition.\n"
            "       Add --bartman-begins to open the original Bartman Begins stage directly.\n"
            "       Add --stage <packaged-map-stem> for an original direct stage audit; --play-stage-intro retains opening movies; --resource-audit <jsonl> saves encounter receipts.\n"
            "       Add --auto-defeat-loc-enemies to defeat NPC enemies in Land of Chocolate.\n"
            "       SimpsonsNative --profile-store <directory> --create-local-profile <name>\n"
            "       SimpsonsNative --profile-store <directory> --list-local-profiles\n"
            "Diagnostic bootstrap; launches are muted. Local profiles provide offline session state; game selection and saves remain under development.\n",e.what());
        return 2;
    }
    std::unique_ptr<Simpsons::Runtime> runtime;
    PPCContext ctx{};
    try {
        if(options.create || options.list) {
            Simpsons::Platform::NativeLocalPlayers store(options.store);
            const auto printProfile=[](const Simpsons::Platform::LocalProfile& profile) {
                if(fprintf(stdout,"%s\t%s\n",profile.id.c_str(),profile.name.c_str())<0)
                    throw std::runtime_error("Unable to write local profile command output");
            };
            if(options.create) {
                const auto profile=store.create(*options.create);
                printProfile(profile);
            } else {
                const auto profiles=store.list();
                for(const auto& profile:profiles)printProfile(profile);
            }
            // Publish captured command output, and detect a failed pipe, before
            // reporting success or beginning process/static teardown.
            if(fflush(stdout)!=0)throw std::runtime_error("Unable to write local profile command output");
            return 0;
        }
        StackSampler::startFromEnvironment();
        runtime=std::make_unique<Simpsons::Runtime>();
        runtime->resourceAudit.configure(options.resourceAudit);
        runtime->auditStage=options.auditStage;
        runtime->auditStagePlayIntro=options.auditStagePlayIntro;
        runtime->renderTestFirstMission=options.renderTestFirstMission;
        runtime->firstMissionCompletion=options.firstMissionCompletion;
        runtime->bartmanBegins=options.bartmanBegins;
        runtime->autoDefeatLocEnemies=options.autoDefeatLocEnemies;
        if(!options.inputRecording.empty())runtime->inputRecording=std::make_shared<Simpsons::Platform::NativeInputRecording>(
            std::filesystem::absolute(options.inputRecording),&runtime->nativeDepthCopyCount,options.inputRecordingAutoStart);
        if(!options.inputPlayback.empty())runtime->inputPlayback=std::make_shared<Simpsons::Platform::NativeInputPlayback>(
            std::filesystem::absolute(options.inputPlayback),options.inputPlaybackStartScene,runtime->nativeDepthCopyCount,
            options.inputPlaybackContinueLive,options.inputPlaybackExpectedEndScene);
        if(!options.controllerInput.empty())runtime->controllerCommands=std::make_shared<Simpsons::Platform::NativeCommandInput>(options.controllerInput);
        if(!options.captures.empty())runtime->frameCaptureDirectory=std::filesystem::absolute(options.captures).lexically_normal();
        runtime->captureOnRequest=options.captureOnRequest;
        runtime->requestedFrameRate=options.frameRate;
        runtime->uncappedFrameRate=options.uncappedFrameRate;
        runtime->vsyncEnabled=options.vsync;
        runtime->videoSettingsPath=std::filesystem::absolute(options.store.empty()?
            options.image.parent_path().parent_path()/"userdata"/"local-profiles":options.store);
        runtime->videoSettingsPath+=L".video.cfg";
        runtime->videoSettings=Simpsons::NativeVideoSettings::load(runtime->videoSettingsPath);
        if(options.frameRate)runtime->videoSettings.frameRate=options.frameRate;
        if(options.uncappedFrameRate)runtime->videoSettings.frameRate=0;
        if(options.vsync)runtime->videoSettings.vsync=true;
        runtime->requestedFrameRate=runtime->videoSettings.frameRate;
        runtime->uncappedFrameRate=runtime->videoSettings.frameRate==0;
        runtime->vsyncEnabled=runtime->videoSettings.vsync;
        wchar_t modulePath[32768]{};
        const DWORD moduleLength=GetModuleFileNameW(nullptr,modulePath,32768);
        if(!moduleLength||moduleLength>=32768)throw std::runtime_error("Native video asset location unavailable");
        runtime->nativeFrontendRoot=std::filesystem::canonical(std::filesystem::path(modulePath).parent_path()/"native-assets");
        if(options.captureOnRequest)std::filesystem::create_directories(runtime->frameCaptureDirectory);
        if(!options.frameTiming.empty())runtime->frameTimingPath=std::filesystem::absolute(options.frameTiming).lexically_normal();
        runtime->frameTimingFramesOnly=options.frameTimingFramesOnly;
        if(!options.store.empty()) runtime->configureLocalPlayers(options.store);
        runtime->load(options.image);
        if(!options.content.empty())runtime->contentRoot=std::filesystem::absolute(options.content).lexically_normal();
        for(uint32_t slot=0;slot<4;++slot) if(options.profiles[slot]) {
            runtime->activateLocalPlayer(slot,*options.profiles[slot]);
            fprintf(stderr,"[LOCAL PLAYER] activated native offline profile slot=%u id=%s; original player association remains unchanged\n",
                slot,options.profiles[slot]->c_str());
        }
        runtime->initialize(ctx);
        fprintf(stderr,"[BOOT] original entry=0x82432280; audio muted; gameplay has not been verified\n");
        std::set_terminate([]{
            try { if(auto error=std::current_exception()) std::rethrow_exception(error); }
            catch(const std::exception& e) { fprintf(stderr,"[TERMINATE] uncaught %s\n",e.what()); }
            catch(...) { fprintf(stderr,"[TERMINATE] uncaught non-standard exception\n"); }
            fflush(stderr);std::abort();
        });
        int status=Simpsons::runOriginal(ctx,runtime->base);
        if(status)runtime->resourceAudit.failure("Original execution stopped with status "+std::to_string(status));
        fprintf(stderr,"[STOP] status=%d; no playable milestone is implied\n",status);
        fflush(stderr);
        return status;
    } catch(const Simpsons::ThreadExit& exit) {
        fprintf(stderr,"[MAIN THREAD EXIT] code=0x%X\n",exit.code);
        fflush(stderr);
        return int(exit.code);
    } catch(const std::exception& e) {
        if(runtime&&std::string_view(e.what())!="Native window closed")runtime->resourceAudit.failure(e.what());
        fprintf(stderr,"[FAILURE] %s\n",e.what());
        if(ctx.lastFunction) {
            fprintf(stderr,"[FAILURE CONTEXT] function=%08X lr=%08X sp=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r28=%08X r29=%08X r30=%08X r31=%08X\n",
                ctx.lastFunction,uint32_t(ctx.lr),ctx.r1.u32,ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,
                ctx.r7.u32,ctx.r8.u32,ctx.r28.u32,ctx.r29.u32,ctx.r30.u32,ctx.r31.u32);
        }
        fflush(stderr);
        if(options.hold && runtime && runtime->window && !runtime->window->closed) {
            fprintf(stderr,"[INSPECTION] Execution stopped. Holding failed window for up to 120 seconds; close to exit.\n");
            ULONGLONG end=GetTickCount64()+120000;
            while(!runtime->window->closed && GetTickCount64()<end) Sleep(10);
        }
        return 1;
    }
}
