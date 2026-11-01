    void FtpStream::RetrieveAsync(const std::string& path, FtpTransferCallback cb) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, path, cb = std::move(cb)]() {
            std::vector<uint8_t> data;
            std::string error;
            bool ok = Retrieve(path, &data, &error);
            cb(ok, data, error);
        });
}

void FtpStream::ListAsync(const std::string& path, FtpListCallback cb) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, path, cb = std::move(cb)]() {
            std::vector<std::string> items;
            std::string error;
            bool ok = List(path, &items, &error);
            cb(ok, items, error);
        });
}
