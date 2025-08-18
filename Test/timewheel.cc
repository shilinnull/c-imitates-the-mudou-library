
#include <functional>
#include <memory>
#include <stdint.h>

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;

class TimerTask
{
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb)
        : _id(id), _time_out(delay), _canceled(false), _task_cb(cb)
    {
    }

    ~TimerTask()
    {
        if (_canceled == false)
            _task_cb();
        _release();
    }

    void SetRelease(const ReleaseFunc &cb) { _release = cb; }
    uint32_t DelayTime() { return _time_out; }
    void Cancel() { _canceled = true; }

private:
    uint64_t _id;         // 任务id
    uint32_t _time_out;   // 超时时间
    bool _canceled;       // false-表示没有被取消， true-表示被取消
    TaskFunc _task_cb;    // 执行的定时任务
    ReleaseFunc _release; // 删除TimerWheel中保存的定时器对象信息
};

class TimerWheel
{
    using PtrTask = std::shared_ptr<TimerTask>;
    using WeakTask = std::weak_ptr<TimerTask>;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _times.find(id);
        if (it != _times.end())
        {
            _times.erase(id);
        }
    }

public:
    TimerWheel()
        : _tick(0), _capacity(60), _wheel(_capacity)
    {
    }

    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt); // 添加到轮子中
        _times[id] = WeakTask(pt); // 保存定时器对象的weak_ptr
    }

    // 刷新/延迟定时任务
    void TimerRefresh(uint64_t id)
    {
        // 通过保存的定时器对象的weak_ptr构造一个shared_ptr出来，添加到轮子中
        auto it = _times.find(id);
        if (it == _times.end())
            return;                     // 没找着定时任务，没法刷新，没法延迟

        PtrTask pt = it->second.lock(); // lock获取weak_ptr管理的对象对应的shared_ptr
        uint32_t delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerCancel(uint64_t id)
    {
        auto it = _times.find(id);
        if (it == _times.end())
            return;                     // 没找着定时任务，没法取消
        PtrTask pt = it->second.lock();
        
    }

private:
    int _tick;     // 秒针，走到哪里释放哪里，相当于执行哪里的任务
    int _capacity; // 最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _times;
};