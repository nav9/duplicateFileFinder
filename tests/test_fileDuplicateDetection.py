import os
import random
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import fileDuplicateFinder
from programConstants import constants

class TestFileDuplicateFinding:
    def test_searchingInEmptyFolder(self):
        #---first create a dummy folder
        fileFolderOps = fileOps.FileOperations()
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + "folder1", "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        #---run a search        
        duplicateFinder = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchWithoutMovingFile()
        duplicateFinder.search()
        assert not duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        
    def test_searchingInFolderWithDuplicateFiles(self):
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderName = "folder2"
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + folderName, "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        nestedFolder = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "nestedFolder", "")
        fileFolderOps.createDirectoryIfNotExisting(nestedFolder)        
        #---create dummy files
        hardcodedSizes = [1024 * 2000, 1, 0] #1024 * 2000 will generate a 2MB file
        filenames = []
        numberOfFilesToGenerate = 5 #Ensure the number of files generated are greater than the number of file sizes being hardcoded below
        for i in range(0, numberOfFilesToGenerate):            
            generatedFilename = os.path.join(folderToSearch, constants.GlobalConstants.dummyPrefix + "file" + str(i))
            randomFileSize = random.randint(0, 1024) #nothing particular about 1024. It could be any other number too
            #hardcode some file sizes
            if i < len(hardcodedSizes):
                randomFileSize = hardcodedSizes[i]
            fileFolderOps.generateFileWithRandomData(generatedFilename, randomFileSize)
            filenames.append(generatedFilename)
        #---create duplicates of the hardcoded size files
        for i in range(0, len(hardcodedSizes)):
            fileFolderOps.copyFile(filenames[i], nestedFolder)
        #---run a search        
        duplicateFinder = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchByMovingFile()
        duplicateFinder.search()
        assert duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
     