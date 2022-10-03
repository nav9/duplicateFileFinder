import os
from fileAndFolder import fileFolderOperations as fileOps
from fileAndFolder import fileDuplicateFinder
from programConstants import constants

class TestFileDuplicateFinding:
    def test_searchingInEmptyFolder(self):
        #---first create a dummy folder  
        fileFolderOps = fileOps.FileOperations()
        folderToSearch = os.path.join(constants.Tests.testFolder, constants.Tests.dummyPrefix + "f1", "") #the quotes at the end add a folder slash if it does not exist
        fileFolderOps.createDirectoryIfNotExisting(folderToSearch)
        #---run a search        
        duplicateFinder = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderToSearch, fileFolderOps)
        duplicateFinder.activateTestMode()
        duplicateFinder.search()
        assert not duplicateFinder.wereDuplicatesFound() #the function will return False if no duplicate files were found
        
