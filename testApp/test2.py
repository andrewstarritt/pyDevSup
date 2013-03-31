
import weakref
import threading, time
from devsup.hooks import addHook
from devsup.db import IOScanListThread
from devsup.util import StoppableThread

insts = {}

def done(obj):
    print obj,'Expires'

class Driver(StoppableThread):
    def __init__(self, name):
        super(Driver,self).__init__()
        self.name = name
        self.scan = IOScanListThread()
        self.value = 0
        addHook('AfterIocRunning', self.start)
        addHook('AtIocExit', self.join)

    def run(self):
        try:
            while self.shouldRun():
                time.sleep(1.0)
                
                val = self.value
                self.value += 1
                self.scan.interrupt(reason=val)
        finally:
            self.finish()

def addDrv(name):
    print 'Create driver',name
    insts[name] = Driver(name)

class Device(object):
    def __init__(self, rec, args):
        self.driver, self.record = insts[args], rec
        self.allowScan = self.driver.scan.add

    def detach(self, rec):
        print 'detach',rec

    def process(self, rec, data):
        print 'test2 Update',rec,data
        if data is not None:
            rec.VAL = data

build = Device
