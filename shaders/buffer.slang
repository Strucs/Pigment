/**
 * Copyright 2026 Angel-Leduc TA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module buffer;

namespace pigment.std {
    public struct Buffer<T, let access : Access = Access.Read> {
        private Ptr<T, access, AddressSpace.Device> address;

        public Ptr<T, access, AddressSpace.Device> ptr_at(uint index)
        {
            return address + index;
        }

        public __subscript(uint index)->T
        {
            get {
                return address[index];
            }

        [nonmutating]
            set {
                static_assert(access == Access.ReadWrite, "Buffer is read-only");
                address[index] = newValue;
            }
        }
    };
}
