#pragma once

#include "Task.hpp"
#include <vector>

class TaskNode {
    friend class TimingWheel;
    friend class Timer;
public:
    TaskNode(Task* t, int time_offset) : task(t), next(nullptr), prev(nullptr), time(time_offset), wheel_index(-1) {}

private:
    Task* task;
    TaskNode* next, *prev;
    int time;
    int wheel_index; // Track which wheel this node is in
};

class TimingWheel {
    friend class Timer;
public:
    TimingWheel(size_t size, size_t interval) : size(size), interval(interval), current_slot(0) {
        slots = new TaskNode*[size];
        for (size_t i = 0; i < size; ++i) {
            slots[i] = nullptr;
        }
    }

    ~TimingWheel() {
        // Clean up all task nodes in all slots
        for (size_t i = 0; i < size; ++i) {
            TaskNode* current = slots[i];
            while (current != nullptr) {
                TaskNode* next = current->next;
                delete current;
                current = next;
            }
        }
        delete[] slots;
    }

    // Add a task node to a specific slot
    void addTaskNode(TaskNode* node, size_t slot_index) {
        slot_index = slot_index % size;
        if (slots[slot_index] == nullptr) {
            slots[slot_index] = node;
            node->next = nullptr;
            node->prev = nullptr;
        } else {
            node->next = slots[slot_index];
            node->prev = nullptr;
            slots[slot_index]->prev = node;
            slots[slot_index] = node;
        }
    }

    // Remove a task node from its slot
    void removeTaskNode(TaskNode* node) {
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            // Node is at the head, need to find which slot
            for (size_t i = 0; i < size; ++i) {
                if (slots[i] == node) {
                    slots[i] = node->next;
                    break;
                }
            }
        }
        if (node->next) {
            node->next->prev = node->prev;
        }
        node->next = nullptr;
        node->prev = nullptr;
    }

    // Get all tasks at current slot and clear the slot
    TaskNode* getCurrentSlotTasks() {
        TaskNode* tasks = slots[current_slot];
        slots[current_slot] = nullptr;
        return tasks;
    }

    // Advance to next slot
    bool tick() {
        current_slot = (current_slot + 1) % size;
        return current_slot == 0; // Return true if wrapped around
    }

private:
    const size_t size, interval;
    size_t current_slot;
    TaskNode** slots;
};

class Timer {
public:
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    Timer() {
        // Create three timing wheels: second (60 slots, 1s), minute (60 slots, 60s), hour (24 slots, 3600s)
        wheels[0] = new TimingWheel(60, 1);      // Second wheel
        wheels[1] = new TimingWheel(60, 60);     // Minute wheel
        wheels[2] = new TimingWheel(24, 3600);   // Hour wheel
    }

    ~Timer() {
        for (int i = 0; i < 3; ++i) {
            delete wheels[i];
        }
    }

    TaskNode* addTask(Task* task) {
        size_t time = task->getFirstInterval();
        TaskNode* node = new TaskNode(task, time);
        addTaskToWheel(node, time);
        return node;
    }

    void cancelTask(TaskNode *p) {
        // Remove from the wheel it belongs to
        if (p->wheel_index >= 0 && p->wheel_index < 3) {
            wheels[p->wheel_index]->removeTaskNode(p);
        }
        delete p;
    }

    std::vector<Task*> tick() {
        std::vector<Task*> result;

        // Advance second wheel
        bool minute_tick = wheels[0]->tick();

        // Process tasks at current second slot
        TaskNode* current = wheels[0]->getCurrentSlotTasks();
        TaskNode* next_node = nullptr;

        while (current != nullptr) {
            next_node = current->next;
            result.push_back(current->task);

            // Reschedule periodic task
            size_t period = current->task->getPeriod();
            if (period > 0) {
                current->time = period;
                current->next = nullptr;
                current->prev = nullptr;
                current->wheel_index = -1;
                addTaskToWheel(current, period);
            } else {
                delete current;
            }

            current = next_node;
        }

        // Handle minute wheel tick
        if (minute_tick) {
            bool hour_tick = wheels[1]->tick();
            cascadeTasks(1);

            // Handle hour wheel tick
            if (hour_tick) {
                wheels[2]->tick();
                cascadeTasks(2);
            }
        }

        return result;
    }

private:
    TimingWheel* wheels[3]; // 0: second, 1: minute, 2: hour

    void addTaskToWheel(TaskNode* node, size_t time) {
        // Determine which wheel and slot to place the task
        if (time / wheels[0]->interval <= wheels[0]->size) {
            // Place in second wheel
            size_t slot = (wheels[0]->current_slot + time / wheels[0]->interval) % wheels[0]->size;
            wheels[0]->addTaskNode(node, slot);
            node->wheel_index = 0;
        } else if (time / wheels[1]->interval <= wheels[1]->size) {
            // Place in minute wheel
            size_t adjusted_time = time + wheels[1]->current_slot * wheels[1]->interval;
            size_t slot = (adjusted_time / wheels[1]->interval) % wheels[1]->size;
            wheels[1]->addTaskNode(node, slot);
            node->wheel_index = 1;
        } else if (time / wheels[2]->interval <= wheels[2]->size) {
            // Place in hour wheel
            size_t adjusted_time = time + wheels[2]->current_slot * wheels[2]->interval;
            size_t slot = (adjusted_time / wheels[2]->interval) % wheels[2]->size;
            wheels[2]->addTaskNode(node, slot);
            node->wheel_index = 2;
        } else {
            // Task is beyond all wheels, delete it
            delete node;
        }
    }

    void cascadeTasks(int wheel_index) {
        // Move tasks from higher wheel to lower wheel
        TaskNode* current = wheels[wheel_index]->getCurrentSlotTasks();
        TaskNode* next_node = nullptr;

        while (current != nullptr) {
            next_node = current->next;

            // Adjust time based on the interval
            current->time = current->time % wheels[wheel_index]->interval;
            current->next = nullptr;
            current->prev = nullptr;
            current->wheel_index = -1;

            addTaskToWheel(current, current->time);

            current = next_node;
        }
    }
};
