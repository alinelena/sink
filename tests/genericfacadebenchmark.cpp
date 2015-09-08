#include <QtTest>

#include <QString>

#include <common/facade.h>
#include <common/domainadaptor.h>
#include <common/resultprovider.h>
#include <common/synclistresult.h>

#include "event_generated.h"

class TestEventAdaptorFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestEventAdaptorFactory()
        : DomainTypeAdaptorFactory()
    {
    }

    virtual ~TestEventAdaptorFactory() {};
};

class TestResourceAccess : public Akonadi2::ResourceAccessInterface
{
    Q_OBJECT
public:
    virtual ~TestResourceAccess() {};
    KAsync::Job<void> sendCommand(int commandId) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> sendCommand(int commandId, flatbuffers::FlatBufferBuilder &fbb) Q_DECL_OVERRIDE { return KAsync::null<void>(); }
    KAsync::Job<void> synchronizeResource(bool remoteSync, bool localSync) Q_DECL_OVERRIDE { return KAsync::null<void>(); }

public Q_SLOTS:
    void open() Q_DECL_OVERRIDE {}
    void close() Q_DECL_OVERRIDE {}
};

class TestResourceFacade : public Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>
{
public:
    TestResourceFacade(const QByteArray &instanceIdentifier, const QSharedPointer<EntityStorage<Akonadi2::ApplicationDomain::Event> > storage, const QSharedPointer<Akonadi2::ResourceAccessInterface> resourceAccess)
        : Akonadi2::GenericFacade<Akonadi2::ApplicationDomain::Event>(instanceIdentifier, QSharedPointer<TestEventAdaptorFactory>::create(), storage, resourceAccess)
    {

    }
    virtual ~TestResourceFacade()
    {

    }
};

class GenericFacadeBenchmark : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    void initTestCase()
    {
        Akonadi2::Storage store(Akonadi2::storageLocation(), "identifier", Akonadi2::Storage::ReadWrite);
        store.removeFromDisk();
    }

    void testLoad()
    {
        const QByteArray identifier = "identifier";
        const int count = 100000;

        //Setup
        auto domainTypeAdaptorFactory = QSharedPointer<TestEventAdaptorFactory>::create();
        {
            Akonadi2::Storage storage(Akonadi2::storageLocation(), identifier, Akonadi2::Storage::ReadWrite);
            auto transaction = storage.createTransaction(Akonadi2::Storage::ReadWrite);
            auto db = transaction.openDatabase();
            for (int i = 0; i < count; i++) {
                auto domainObject = Akonadi2::ApplicationDomain::Event::Ptr::create();
                domainObject->setProperty("uid", "uid");
                domainObject->setProperty("summary", "summary");

                flatbuffers::FlatBufferBuilder fbb;
                domainTypeAdaptorFactory->createBuffer(*domainObject, fbb);
                db.write(QString::number(i).toLatin1(), QByteArray::fromRawData(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize()));
            }
        }

        Akonadi2::Query query;
        query.liveQuery = false;

        //Benchmark
        QBENCHMARK {
            auto resultSet = QSharedPointer<Akonadi2::ResultProvider<Akonadi2::ApplicationDomain::Event::Ptr> >::create();
            auto resourceAccess = QSharedPointer<TestResourceAccess>::create();
            auto storage = QSharedPointer<EntityStorage<Akonadi2::ApplicationDomain::Event> >::create("identifier", domainTypeAdaptorFactory, "bufferType");
            TestResourceFacade facade(identifier, storage, resourceAccess);

            async::SyncListResult<Akonadi2::ApplicationDomain::Event::Ptr> result(resultSet->emitter());

            facade.load(query, resultSet).exec().waitForFinished();
            resultSet->initialResultSetComplete();

            //We have to wait for the events that deliver the results to be processed by the eventloop
            result.exec();

            QCOMPARE(result.size(), count);
        }

        // Print memory layout, RSS is what is in memory
        // std::system("exec pmap -x \"$PPID\"");
    }
};

QTEST_MAIN(GenericFacadeBenchmark)
#include "genericfacadebenchmark.moc"