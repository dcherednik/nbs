from cloud.storage.core.tools.common.python.daemon import Daemon
from contrib.ydb.tests.library.harness.kikimr_runner import get_unique_path_for_current_test, ensure_path_exists
import contrib.ydb.tests.library.common.yatest_common as yatest_common

from cloud.disk_manager.test.common.processes import register_process, kill_processes

SERVICE_NAME = "image_file_server"


class ImageFileServer(Daemon):

    def __init__(self, port, working_dir, image_file_path, other_image_file_path):
        command = [yatest_common.binary_path(
            "cloud/disk_manager/test/images/server/server")]
        command += [
            "start",
            "--image-file-path", image_file_path,
            "--other-image-file-path", other_image_file_path,
            "--port", str(port),
        ]

        super(ImageFileServer, self).__init__(
            commands=[command],
            cwd=working_dir)


class ImageFileServerLauncher:

    def __init__(self, image_file_path, other_image_file_path=""):
        self.__image_file_path = image_file_path
        self.__other_image_file_path = other_image_file_path

        self.__port_manager = yatest_common.PortManager()
        self.__port = self.__port_manager.get_port()

        working_dir = get_unique_path_for_current_test(
            output_path=yatest_common.output_path(),
            sub_folder=""
        )
        ensure_path_exists(working_dir)

        self.__daemon = ImageFileServer(
            self.__port,
            working_dir,
            self.__image_file_path,
            self.__other_image_file_path)

    def start(self):
        self.__daemon.start()
        register_process(SERVICE_NAME, self.__daemon.pid)

    @staticmethod
    def stop():
        kill_processes(SERVICE_NAME)

    @property
    def port(self):
        return self.__port
