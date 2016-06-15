/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <functional>
#include <memory>
#include "resultset.h"
#include "log.h"

namespace Sink {

/**
* Query result set
*/
template <class T>
class ResultEmitter;

template <class T>
class ResultProviderInterface
{
public:
    ResultProviderInterface() : mRevision(0)
    {
    }

    virtual ~ResultProviderInterface()
    {
    }

    virtual void add(const T &value) = 0;
    virtual void modify(const T &value) = 0;
    virtual void remove(const T &value) = 0;
    virtual void initialResultSetComplete(const T &parent) = 0;
    virtual void complete() = 0;
    virtual void clear() = 0;
    virtual void setFetcher(const std::function<void(const T &parent)> &fetcher) = 0;

    void setRevision(qint64 revision)
    {
        mRevision = revision;
    }

    qint64 revision() const
    {
        return mRevision;
    }

private:
    qint64 mRevision;
};

/*
* The promise side for the result emitter
*/
template <class T>
class ResultProvider : public ResultProviderInterface<T>
{
public:
    typedef QSharedPointer<ResultProvider<T>> Ptr;

    virtual ~ResultProvider()
    {
    }

    // Called from worker thread
    void add(const T &value)
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->addHandler(value);
        }
    }

    void modify(const T &value)
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->modifyHandler(value);
        }
    }

    void remove(const T &value)
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->removeHandler(value);
        }
    }

    void initialResultSetComplete(const T &parent)
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->initialResultSetComplete(parent);
        }
    }

    // Called from worker thread
    void complete()
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->complete();
        }
    }

    void clear()
    {
        if (auto strongRef = mResultEmitter.toStrongRef()) {
            strongRef->clear();
        }
    }


    QSharedPointer<ResultEmitter<T>> emitter()
    {
        if (!mResultEmitter) {
            // We have to go over a separate var and return that, otherwise we'd delete the emitter immediately again
            auto sharedPtr = QSharedPointer<ResultEmitter<T>>(new ResultEmitter<T>, [this](ResultEmitter<T> *emitter) {
                done();
                delete emitter;
            });
            mResultEmitter = sharedPtr;
            sharedPtr->setFetcher([this](const T &parent) {
                Q_ASSERT(mFetcher);
                mFetcher(parent);
            });
            return sharedPtr;
        }

        return mResultEmitter.toStrongRef();
    }

    void onDone(const std::function<void()> &callback)
    {
        mOnDoneCallback = callback;
    }

    bool isDone() const
    {
        // The existance of the emitter currently defines wether we're done or not.
        return mResultEmitter.toStrongRef().isNull();
    }

    void setFetcher(const std::function<void(const T &parent)> &fetcher)
    {
        mFetcher = fetcher;
    }

private:
    void done()
    {
        if (mOnDoneCallback) {
            auto callback = mOnDoneCallback;
            mOnDoneCallback = std::function<void()>();
            // This may delete this object
            callback();
        }
    }

    QWeakPointer<ResultEmitter<T>> mResultEmitter;
    std::function<void()> mOnDoneCallback;
    std::function<void(const T &parent)> mFetcher;
};

/*
* The future side for the client.
*
* It does not directly hold the state.
*
* The advantage of this is that we can specialize it to:
* * do inline transformations to the data
* * directly store the state in a suitable datastructure: QList, QSet, std::list, QVector, ...
* * build async interfaces with signals
* * build sync interfaces that block when accessing the value
*
*/
template <class DomainType>
class ResultEmitter
{
public:
    typedef QSharedPointer<ResultEmitter<DomainType>> Ptr;

    virtual ~ResultEmitter()
    {
    }

    void onAdded(const std::function<void(const DomainType &)> &handler)
    {
        addHandler = handler;
    }

    void onModified(const std::function<void(const DomainType &)> &handler)
    {
        modifyHandler = handler;
    }

    void onRemoved(const std::function<void(const DomainType &)> &handler)
    {
        removeHandler = handler;
    }

    void onInitialResultSetComplete(const std::function<void(const DomainType &)> &handler)
    {
        initialResultSetCompleteHandler = handler;
    }

    void onComplete(const std::function<void(void)> &handler)
    {
        completeHandler = handler;
    }

    void onClear(const std::function<void(void)> &handler)
    {
        clearHandler = handler;
    }

    void add(const DomainType &value)
    {
        addHandler(value);
    }

    void modify(const DomainType &value)
    {
        modifyHandler(value);
    }

    void remove(const DomainType &value)
    {
        removeHandler(value);
    }

    void initialResultSetComplete(const DomainType &parent)
    {
        if (initialResultSetCompleteHandler) {
            initialResultSetCompleteHandler(parent);
        }
    }

    void complete()
    {
        if (completeHandler) {
            completeHandler();
        }
    }

    void clear()
    {
        if (clearHandler) {
            clearHandler();
        }
    }

    void setFetcher(const std::function<void(const DomainType &parent)> &fetcher)
    {
        mFetcher = fetcher;
    }

    virtual void fetch(const DomainType &parent)
    {
        if (mFetcher) {
            mFetcher(parent);
        }
    }

private:
    friend class ResultProvider<DomainType>;

    std::function<void(const DomainType &)> addHandler;
    std::function<void(const DomainType &)> modifyHandler;
    std::function<void(const DomainType &)> removeHandler;
    std::function<void(const DomainType &)> initialResultSetCompleteHandler;
    std::function<void(void)> completeHandler;
    std::function<void(void)> clearHandler;

    std::function<void(const DomainType &parent)> mFetcher;
};

template <class DomainType>
class AggregatingResultEmitter : public ResultEmitter<DomainType>
{
public:
    typedef QSharedPointer<AggregatingResultEmitter<DomainType>> Ptr;

    void addEmitter(const typename ResultEmitter<DomainType>::Ptr &emitter)
    {
        Q_ASSERT(emitter);
        emitter->onAdded([this](const DomainType &value) { this->add(value); });
        emitter->onModified([this](const DomainType &value) { this->modify(value); });
        emitter->onRemoved([this](const DomainType &value) { this->remove(value); });
        auto ptr = emitter.data();
        emitter->onInitialResultSetComplete([this, ptr](const DomainType &parent) {
            auto hashValue = qHash(parent);
            mInitialResultSetInProgress.remove(hashValue, ptr);
            callInitialResultCompleteIfDone(parent);
        });
        emitter->onComplete([this]() { this->complete(); });
        emitter->onClear([this]() { this->clear(); });
        mEmitter << emitter;
    }

    void callInitialResultCompleteIfDone(const DomainType &parent)
    {
        auto hashValue = qHash(parent);
        // Normally a parent is only in a single resource, except the toplevel (invalid) parent
        if (!mInitialResultSetInProgress.contains(hashValue) && mAllResultsFetched && !mResultEmitted) {
            mResultEmitted = true;
            this->initialResultSetComplete(parent);
        }
    }

    void fetch(const DomainType &parent) Q_DECL_OVERRIDE
    {
        if (mEmitter.isEmpty()) {
            this->initialResultSetComplete(parent);
        } else {
            mResultEmitted = false;
            mAllResultsFetched = false;
            for (const auto &emitter : mEmitter) {
                mInitialResultSetInProgress.insert(qHash(parent), emitter.data());
                emitter->fetch(parent);
            }
            mAllResultsFetched = true;
            callInitialResultCompleteIfDone(parent);
        }
    }

private:
    QList<typename ResultEmitter<DomainType>::Ptr> mEmitter;
    QMultiMap<qint64, ResultEmitter<DomainType> *> mInitialResultSetInProgress;
    bool mAllResultsFetched;
    bool mResultEmitted;
};
}
