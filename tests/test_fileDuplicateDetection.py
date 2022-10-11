import os
import random
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import fileDuplicateFinder
from programConstants import constants

class TestFileDuplicateFinding:
    def test_searchingInEmptyFolder(self):#duplicateFilesFolder should not get created
        #---first create a dummy folder
        fileFolderOps = fileOps.FileOperations()
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.GlobalConstants.dummyPrefix + "folderEmpty", "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.deleteFolderIfItExists(folderToSearch)
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        #---run a search        
        duplicateFinder = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchWithoutMovingFile()
        duplicateFinder.search()
        assert not duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        subFolders = fileFolderOps.getListOfSubfoldersInThisFolder(folderToSearch)
        assert len(subFolders) == 0 #no duplicateFilesFolder generated        
        
    def test_searchingInFolderWithDuplicateFiles(self):#positive test
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderName = "folderWithFile"
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
            #---hardcode some file sizes
            if i < len(hardcodedSizes):
                randomFileSize = hardcodedSizes[i]
            fileFolderOps.generateBinaryFileWithRandomData(generatedFilename, randomFileSize)
            filenames.append(generatedFilename)
        #---create duplicates of the hardcoded size files
        numberOfDuplicatesCreated = 0
        for i in range(0, len(hardcodedSizes)):
            fileFolderOps.copyFile(filenames[i], nestedFolder)
            numberOfDuplicatesCreated += 1
        #---create a symbolic link that should be ignored
        symlinkFilenameWithPath = os.path.join(folderToSearch, "linkToNowhere")
        fileFolderOps.deleteFileIfItExists(symlinkFilenameWithPath)
        os.symlink(folderToSearch, symlinkFilenameWithPath) #Create the symlink file which should be ignored during traversal
        #---create a duplicate of the symlink
        fileFolderOps.copyFile(symlinkFilenameWithPath, nestedFolder) #this copied file should not end up in the duplicates folder
        #---run a search        
        duplicateFinder = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderToSearch, fileFolderOps)
        duplicateFinder.switchOffGUI()
        duplicateFinder.setToSearchByMovingFile()
        duplicateFinder.search()
        assert duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        #---check if files were copied to duplicate folder
        duplicates = fileFolderOps.getListOfFilesInThisFolder(os.path.join(folderToSearch, constants.GlobalConstants.duplicateFilesFolder))
        extraFilesAllowedInDuplicatesFolder = ["the undo file"]
        assert len(duplicates) == numberOfDuplicatesCreated + len(extraFilesAllowedInDuplicatesFolder) #files found should be equal to the number of duplicates we know were created plus the undo file generated
        #---check if undo file is present
        undoPresent = False
        for fileInDuplicatesFolder in duplicates:
            if fileInDuplicatesFolder.endswith(constants.GlobalConstants.UNDO_FILE_EXTENSION):
                undoPresent = True
                break
        assert undoPresent
        #TODO: For the sake of completeness, it's also necessary to check for whether the files were actually moved to the duplicates folder, rather than copied. So it's necessary to check for the number of files remaining in folders other than the duplicate folder.
                