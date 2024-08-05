#include "gpu/async_upload.h"

namespace Raster {
    std::mutex AsyncUpload::m_infoMutex;
    std::unordered_map<int, AsyncUploadInfo> AsyncUpload::m_infos;
    std::thread AsyncUpload::m_uploader;
    bool AsyncUpload::m_running = false;

    AsyncUploadInfo::AsyncUploadInfo() {
        this->ready = false;
    }

    void AsyncUpload::Initialize() {
        std::cout << "booting up async uploader" << std::endl;
        m_running = true;
        m_uploader = std::thread(AsyncUpload::UploaderLogic);
    }

    void AsyncUpload::Terminate() {
        m_running = false;
        m_uploader.join();
    }

    AsyncUploadInfoID AsyncUpload::GenerateTextureFromImage(std::shared_ptr<Image> image) {
        int uploadID = Randomizer::GetRandomInteger();
        AsyncUploadInfo info;
        info.image = image;
        info.ready = false;
        info.texture = Texture();

        SyncPutAsyncUploadInfo(uploadID, info);

        return uploadID;
    }

    void AsyncUpload::DestroyTexture(Texture texture) {
        AsyncUploadInfo info;
        info.deleteTexture = texture;
        SyncPutAsyncUploadInfo(Randomizer::GetRandomInteger(), info);
    }

    bool AsyncUpload::IsUploadReady(AsyncUploadInfoID t_id) {
        if (SyncAsyncUploadInfoExists(t_id)) {
            auto info = SyncGetAsyncUploadInfo(t_id);
            return info.ready;
        }
        return false;
    }

    void AsyncUpload::DestroyUpload(AsyncUploadInfoID& t_id) {
        if (SyncAsyncUploadInfoExists(t_id)) {
            SyncDestroyAsyncUploadInfo(t_id);
            t_id = 0;
        }
    }

    AsyncUploadInfo& AsyncUpload::GetUpload(AsyncUploadInfoID t_id) {
        return SyncGetAsyncUploadInfo(t_id);
    }

    void AsyncUpload::UploaderLogic() {
        GPU::InitializeContext();

        std::vector<int> skipID;
        while (m_running) {
            if (SyncIsInfosEmpty()) continue; 
            auto pair = SyncGetFirstAsyncUploadInfo();
            auto& info = pair.second;
            bool deleted = false;
            if (std::find(skipID.begin(), skipID.end(), pair.first) != skipID.end()) continue;

            if (info.deleteTexture.handle) {
                GPU::DestroyTexture(info.deleteTexture);
                SyncDestroyAsyncUploadInfo(pair.first);
                continue;
            }

            TexturePrecision precision = TexturePrecision::Usual;
            if (info.image->precision == ImagePrecision::Half) precision = TexturePrecision::Half;
            if (info.image->precision == ImagePrecision::Full) precision = TexturePrecision::Full;

            auto generatedTexture = GPU::GenerateTexture(info.image->width, info.image->height, info.image->channels, precision);
            GPU::UpdateTexture(generatedTexture, 0, 0, info.image->width, info.image->height, info.image->channels, info.image->data.data());
            GPU::Flush();

            info.texture = generatedTexture;
            *info.image = Image();
            info.image = nullptr;
            info.ready = true;

            SyncPutAsyncUploadInfo(pair.first, info);
            skipID.push_back(pair.first);
        }
    }

    void AsyncUpload::SyncDestroyAsyncUploadInfo(AsyncUploadInfoID t_id) {
        std::lock_guard<std::mutex> lg(m_infoMutex);
        m_infos.erase(t_id);
    }

    void AsyncUpload::SyncPutAsyncUploadInfo(int t_key, AsyncUploadInfo t_info) {
        std::lock_guard<std::mutex> lg(m_infoMutex); 
        m_infos[t_key] = t_info;
    }

    AsyncUploadInfo& AsyncUpload::SyncGetAsyncUploadInfo(int t_key) {
        std::lock_guard<std::mutex> lg(m_infoMutex); 
        return m_infos[t_key];
    }

    bool AsyncUpload::SyncIsInfosEmpty() {
        std::lock_guard<std::mutex> lg(m_infoMutex); 
        return m_infos.empty();
    }

    std::pair<int, AsyncUploadInfo> AsyncUpload::SyncGetFirstAsyncUploadInfo() {
        std::lock_guard<std::mutex> lg(m_infoMutex); 
        return *m_infos.begin();
    }

    bool AsyncUpload::SyncAsyncUploadInfoExists(AsyncUploadInfoID t_id) {
        std::lock_guard<std::mutex> lg(m_infoMutex); 
        return m_infos.find(t_id) != m_infos.end();
    }
};