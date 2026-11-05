// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include <tuple>
#include <utility>

#define SAFE_INVOKE_METHOD_IN_GUI_THREAD(object, method, ...)                                   \
    do                                                                                          \
    {                                                                                           \
        auto* _an_obj = (object);                                                               \
        if (QThread::currentThread() == qApp->thread())                                         \
        {                                                                                       \
            _an_obj->method(__VA_ARGS__);                                                       \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            auto _an_args = std::make_tuple(__VA_ARGS__);                                       \
            QMetaObject::invokeMethod(                                                          \
                _an_obj,                                                                        \
                [_an_obj, _an_args = std::move(_an_args)]() mutable {                           \
                    std::apply(                                                                 \
                        [_an_obj](auto&&... _an_values) {                                       \
                            _an_obj->method(std::forward<decltype(_an_values)>(_an_values)...); \
                        },                                                                      \
                        std::move(_an_args));                                                   \
                },                                                                              \
                Qt::QueuedConnection);                                                          \
        }                                                                                       \
    } while (0)

// The _BLOCK forms wait until the GUI thread has run the call. Never use one from a task on the global thread pool:
// the application's closing waits for that pool on the GUI thread, so a task waiting there for the GUI thread is never
// answered and the closing hangs.
#define SAFE_INVOKE_METHOD_IN_GUI_THREAD_BLOCK(object, method, ...)                             \
    do                                                                                          \
    {                                                                                           \
        auto* _an_obj = (object);                                                               \
        if (QThread::currentThread() == qApp->thread())                                         \
        {                                                                                       \
            _an_obj->method(__VA_ARGS__);                                                       \
        }                                                                                       \
        else                                                                                    \
        {                                                                                       \
            auto _an_args = std::make_tuple(__VA_ARGS__);                                       \
            QMetaObject::invokeMethod(                                                          \
                _an_obj,                                                                        \
                [_an_obj, _an_args = std::move(_an_args)]() mutable {                           \
                    std::apply(                                                                 \
                        [_an_obj](auto&&... _an_values) {                                       \
                            _an_obj->method(std::forward<decltype(_an_values)>(_an_values)...); \
                        },                                                                      \
                        std::move(_an_args));                                                   \
                },                                                                              \
                Qt::BlockingQueuedConnection);                                                  \
        }                                                                                       \
    } while (0)

#define SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(object, _lambda)                       \
    do                                                                          \
    {                                                                           \
        if (QThread::currentThread() == qApp->thread())                         \
        {                                                                       \
            _lambda();                                                          \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            QMetaObject::invokeMethod((object), _lambda, Qt::QueuedConnection); \
        }                                                                       \
    } while (0)

#define SAFE_INVOKE_LAMBDA_IN_GUI_THREAD_BLOCK(object, _lambda)                         \
    do                                                                                  \
    {                                                                                   \
        if (QThread::currentThread() == qApp->thread())                                 \
        {                                                                               \
            _lambda();                                                                  \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            QMetaObject::invokeMethod((object), _lambda, Qt::BlockingQueuedConnection); \
        }                                                                               \
    } while (0)