import os
import random
from fileAndFolder import fileFolderOperations as fileOps
from programConstants import constants
from fileAndFolder import undo

class TestUndoFunctionality:
    def test_undoFunctionality(self):#positive test
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderName = "folderToCheckUndo"
        dummyOriginalFolder = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + folderName, "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(dummyOriginalFolder)
        fileFolderOps.createDirectoryIfNotExisting(dummyOriginalFolder)
        dummyDuplicatesFolder = os.path.join(dummyOriginalFolder, constants.GlobalConstants.duplicateFilesFolder, "")
        fileFolderOps.createDirectoryIfNotExisting(dummyDuplicatesFolder)        
        self.undoStore = undo.Undo(dummyDuplicatesFolder, fileFolderOps)
        self.undoStore.switchOffGUI()
        #---create dummy files in the nested folder (these are assumed to be )
        filenames = []
        numberOfFilesToGenerate = 5 #Ensure the number of files generated are greater than the number of file sizes being hardcoded below
        for i in range(0, numberOfFilesToGenerate): 
            newFilename = constants.GlobalConstants.dummyPrefix + "file" + str(i)
            generatedFilename = os.path.join(dummyDuplicatesFolder, newFilename)
            randomFileSize = random.randint(0, 1024) #nothing particular about 1024. It could be any other number too
            fileFolderOps.generateBinaryFileWithRandomData(generatedFilename, randomFileSize)
            filenames.append(generatedFilename)
            #---pretend that the files in the dummy duplicates folder were earlier in the dummy original folder. Keeping the old and new filenames same for simplicity
            self.undoStore.add(dummyOriginalFolder, newFilename, dummyDuplicatesFolder, newFilename)
        undoFilenameWithPath = self.undoStore.generateUndoFile()        
        #---check if undo file got generated
        assert fileFolderOps.isValidFile(undoFilenameWithPath)
        #---do the undo        
        self.undoStore.performUndo(undoFilenameWithPath)
        #---check if files got moved to the "original" folder
        restoredFiles = fileFolderOps.getListOfFilesInThisFolder(dummyOriginalFolder)
        assert len(restoredFiles) == len(filenames)
        #---check that the undo file was deleted
        filesInDuplicateFolder = fileFolderOps.getListOfFilesInThisFolder(dummyDuplicatesFolder)
        assert len(filesInDuplicateFolder) == 0

        