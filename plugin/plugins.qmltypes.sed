# While "DisplayBlanking" originally was a singleton, starting
# from Nemo.KeepAlive 1.2 that is no longer the case. However
# as long as these older API versions are supported, qmlplugindump
# considers the component still to be a singleton -> the output
# must be manually tweaked to match the latest version.
/^\s*isCreatable:/ s@^ \{0,2\}@//@
/^\s*isSingleton:/ s@^ \{0,2\}@//@
