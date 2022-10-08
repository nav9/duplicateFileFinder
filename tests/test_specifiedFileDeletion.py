import os
import random
from fileAndFolder import fileFolderOperations as fileOps
from programConstants import constants
from fileAndFolder import deleter

#TODO: The filenames may need to take into account various operating system conventions of filenames

class Common:
    def __init__(self):
        self.folderName = "folderToCheckFileDeletion"
        self.nonTargetFiles = ["Tho.bd", "thumb.txt", "thanks.jpg", "think.dbb", "Desktop.ini", "somefile.json"]
        self.caseInsensitive = ["Thumbs.db", "thumbs.DB", "tHumbS.dB"]
        self.caseSensitive = ["thumbs.db"]
        self.wildcard1 = [".thumbs.db", "tthumbs.db", "thumbs4.dbb", "thumbs1.db", "thumbs_.db.png"]  #to test for *.db
        self.wildcard2 = ["somefile.db", "th.db."]
        self.folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + self.folderName, "") #the quotes at the end add a folder slash if it does not exist        
        self.fileFolderOps = fileOps.FileOperations()        

    def prepareFolderAfresh(self):
        self.fileFolderOps.deleteFolderIfItExists(self.folderToSearch)
        self.fileFolderOps.createDirectoryIfNotExisting(self.folderToSearch)
                
    def areTheExactNumberOfFilesPresentInBoth(self, list1, list2):
        listsHaveSameElements = False
        if len(list1) == len(list2): #avoiding the expensive sort
            if sorted(list1) == sorted(list2):
                listsHaveSameElements = True
        return listsHaveSameElements        

class TestFileDeletionFunctionality:
    def test_caseSensitiveDeletion(self):        
        common = Common()
        common.prepareFolderAfresh()
        fileDeleter = deleter.FileDeleter(common.folderToSearch, common.fileFolderOps)
        fileDeleter.switchOffGUI()
        #---create a list of filenames to test
        filenamesToGenerate = []; filesThatShouldGetDeleted = []; filesThatShouldNotGetDeleted = []
        filesThatShouldGetDeleted.extend(common.caseSensitive)
        filesThatShouldNotGetDeleted.extend(common.caseInsensitive)
        filesThatShouldNotGetDeleted.extend(common.nonTargetFiles)
        filesThatShouldNotGetDeleted.extend(common.wildcard1)
        filenamesToGenerate.extend(filesThatShouldGetDeleted)
        filenamesToGenerate.extend(filesThatShouldNotGetDeleted)
        simulatedUserInput = ["thumbs.db"]
        #---generate the files
        for file in filenamesToGenerate: 
            generatedFilename = os.path.join(common.folderToSearch, file)
            randomFileSize = random.randint(0, 1024) #nothing particular about 1024. It could be any other number too
            common.fileFolderOps.generateFileWithRandomData(generatedFilename, randomFileSize) 
        #---delete files
        caseSensitive = True
        fileDeleter.searchAndDestroy(simulatedUserInput, caseSensitive)
        #---check which files got deleted and which didn't
        remainingFiles = common.fileFolderOps.getListOfFilesInThisFolder(common.folderToSearch)
        assert common.areTheExactNumberOfFilesPresentInBoth(remainingFiles, filesThatShouldNotGetDeleted)
        
    def test_caseInSensitiveDeletion(self):        
        common = Common()
        common.prepareFolderAfresh()
        fileDeleter = deleter.FileDeleter(common.folderToSearch, common.fileFolderOps)
        fileDeleter.switchOffGUI()
        #---create a list of filenames to test
        filenamesToGenerate = []; filesThatShouldGetDeleted = []; filesThatShouldNotGetDeleted = []
        filesThatShouldGetDeleted.extend(common.caseInsensitive)
        filesThatShouldGetDeleted.extend(common.caseSensitive)
        filesThatShouldNotGetDeleted.extend(common.nonTargetFiles)
        filesThatShouldNotGetDeleted.extend(common.wildcard1)
        filenamesToGenerate.extend(filesThatShouldGetDeleted)
        filenamesToGenerate.extend(filesThatShouldNotGetDeleted)
        simulatedUserInput = ["thumbs.db"]
        #---generate the files
        for file in filenamesToGenerate: 
            generatedFilename = os.path.join(common.folderToSearch, file)
            randomFileSize = random.randint(0, 1024) #nothing particular about 1024. It could be any other number too
            common.fileFolderOps.generateFileWithRandomData(generatedFilename, randomFileSize) 
        #---delete files
        caseSensitive = False
        fileDeleter.searchAndDestroy(simulatedUserInput, caseSensitive)
        #---check which files got deleted and which didn't
        remainingFiles = common.fileFolderOps.getListOfFilesInThisFolder(common.folderToSearch)
        assert common.areTheExactNumberOfFilesPresentInBoth(remainingFiles, filesThatShouldNotGetDeleted)        