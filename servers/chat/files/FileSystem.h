#pragma once
#include "FileWorker.h"
#include "Singleton.h"
#include <memory>
#include <vector>
class FileSystem : public Singleton<FileSystem> {
    friend class Singleton<FileSystem>;

  public:
    ~FileSystem();
    void PostMsgToQue(std::shared_ptr<FileTask> msg, int index);

  private:
    FileSystem();
    std::vector<std::shared_ptr<FileWorker>> _file_workers;
};
