// Android: the assets out of the APK, the app's folder, HTTPS through Java (no curl there).

#ifdef __ANDROID__

#include "android.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <jni.h>

extern "C" struct android_app* GetAndroidApp(void);  // raylib's

namespace {

std::string read_asset(AAssetManager* am, const std::string& name) {
    AAsset* a = AAssetManager_open(am, name.c_str(), AASSET_MODE_STREAMING);
    if (!a) return {};
    std::string out(size_t(AAsset_getLength(a)), '\0');
    AAsset_read(a, out.data(), out.size());
    AAsset_close(a);
    return out;
}

}  // namespace

std::string android_files_dir() {
    return std::string(GetAndroidApp()->activity->internalDataPath) + "/";
}

std::string android_assets_dir() {
    // files.txt (made by the build): a stamp of the build, then the assets' paths. When the stamp
    // differs from the one copied last, all of them are copied again.
    namespace fs = std::filesystem;
    AAssetManager* am = GetAndroidApp()->activity->assetManager;
    const std::string dir = android_files_dir() + "assets";
    const std::string list = read_asset(am, "files.txt");
    std::istringstream in(list);
    std::string stamp;
    std::getline(in, stamp);
    std::string had;
    std::getline(std::ifstream(dir + "/.stamp"), had);
    if (stamp.empty() || had == stamp) return dir;
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (std::string path; std::getline(in, path);) {
        if (path.empty()) continue;
        const fs::path to = fs::path(dir) / path;
        fs::create_directories(to.parent_path(), ec);
        const std::string data = read_asset(am, "assets/" + path);
        std::ofstream(to, std::ios::binary).write(data.data(), std::streamsize(data.size()));
    }
    std::ofstream(dir + "/.stamp") << stamp << "\n";
    return dir;
}

bool android_http_get(const std::string& url, std::vector<uint8_t>& out, int timeout_seconds) {
    // com.zxremakes.app.Net.get (java/), on whatever thread asks: attached for the call, the class
    // found through the activity's class loader (a native thread's own loader knows only the system's)
    out.clear();
    ANativeActivity* act = GetAndroidApp()->activity;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (act->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (act->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    bool ok = false;
    jclass activity = env->GetObjectClass(act->clazz);
    jmethodID get_loader = env->GetMethodID(activity, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = env->CallObjectMethod(act->clazz, get_loader);
    jclass loader_class = env->FindClass("java/lang/ClassLoader");
    jmethodID load = env->GetMethodID(loader_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("com.zxremakes.app.Net");
    auto net = static_cast<jclass>(env->CallObjectMethod(loader, load, name));
    if (!env->ExceptionCheck() && net) {
        jmethodID get = env->GetStaticMethodID(net, "get", "(Ljava/lang/String;I)[B");
        jstring jurl = env->NewStringUTF(url.c_str());
        auto bytes = static_cast<jbyteArray>(env->CallStaticObjectMethod(net, get, jurl, jint(timeout_seconds * 1000)));
        if (!env->ExceptionCheck() && bytes) {
            const jsize n = env->GetArrayLength(bytes);
            out.resize(size_t(n));
            env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(out.data()));
            ok = true;
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) act->vm->DetachCurrentThread();
    return ok;
}

#endif
